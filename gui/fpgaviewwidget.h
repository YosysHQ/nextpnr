/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Serge Bazanski <q3k@symbioticeda.com>
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#ifndef MAPGLWIDGET_H
#define MAPGLWIDGET_H

#include <boost/optional.hpp>
#include <QMainWindow>
#include <QMutex>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPainter>
#include <QThread>
#include <QTimer>
#include <QWaitCondition>

#include "nextpnr.h"
#include "quadtree.h"
#include "lineshader.h"
#include "designwidget.h"

NEXTPNR_NAMESPACE_BEGIN

class PeriodicRunner : public QThread
{
    Q_OBJECT
  private:
    QMutex mutex_;
    QWaitCondition condition_;
    bool abort_;
    std::function<void()> target_;
    QTimer timer_;

  public:
    explicit PeriodicRunner(QObject *parent, std::function<void()> target)
            : QThread(parent), abort_(false), target_(target), timer_(this)
    {
        connect(&timer_, &QTimer::timeout, this, &PeriodicRunner::poke);
    }

    void run(void) override
    {
        for (;;) {
            mutex_.lock();
            condition_.wait(&mutex_);

            if (abort_) {
                mutex_.unlock();
                return;
            }

            target_();

            mutex_.unlock();
        }
    }

    void startTimer(int msecs) { timer_.start(msecs); }

    ~PeriodicRunner()
    {
        mutex_.lock();
        abort_ = true;
        condition_.wakeOne();
        mutex_.unlock();

        wait();
    }

    void poke(void) { condition_.wakeOne(); }
};

class FPGAViewWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

  public:
    FPGAViewWidget(QWidget *parent = 0);
    ~FPGAViewWidget();

  protected:
    // Qt callbacks.
    void initializeGL() Q_DECL_OVERRIDE;
    void paintGL() Q_DECL_OVERRIDE;
    void resizeGL(int width, int height) Q_DECL_OVERRIDE;
    void mousePressEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    void mouseMoveEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    void wheelEvent(QWheelEvent *event) Q_DECL_OVERRIDE;
    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;


  public Q_SLOTS:
    void newContext(Context *ctx);
    void onSelectedArchItem(std::vector<DecalXY> decals);
    void onHighlightGroupChanged(std::vector<DecalXY> decals, int group);
    void pokeRenderer(void);
    void zoomIn();
    void zoomOut();
    void zoomSelected();
    void zoomOutbound();

  Q_SIGNALS:
    void clickedBel(BelId bel);
    void clickedWire(WireId wire);

  private:
    const float zoomNear_ = 0.1f;    // do not zoom closer than this
    const float zoomFar_ = 100.0f; // do not zoom further than this
    const float zoomLvl1_ = 1.0f;
    const float zoomLvl2_ = 5.0f;

    struct PickedElement {
        ElementType type;
        union Inner {
            BelId bel;
            WireId wire;
            PipId pip;
            GroupId group;

            Inner(BelId _bel) : bel(_bel) {}
            Inner(WireId _wire) : wire(_wire) {}
            Inner(PipId _pip) : pip(_pip) {}
            Inner(GroupId _group) : group(_group) {}
        } element;
        float x, y; // Decal X and Y
        PickedElement(BelId bel, float x, float y) : type(ElementType::BEL), element(bel), x(x), y(y) {}
        PickedElement(WireId wire, float x, float y) : type(ElementType::WIRE), element(wire), x(x), y(y) {}
        PickedElement(PipId pip, float x, float y) : type(ElementType::PIP), element(pip), x(x), y(y) {}
        PickedElement(GroupId group, float x, float y) : type(ElementType::GROUP), element(group), x(x), y(y) {}

        DecalXY decal(Context *ctx) const
        {
            DecalXY decal;
            switch (type) {
            case ElementType::BEL:
                decal = ctx->getBelDecal(element.bel);
                break;
            case ElementType::WIRE:
                decal = ctx->getWireDecal(element.wire);
                break;
            case ElementType::PIP:
                decal = ctx->getPipDecal(element.pip);
                break;
            case ElementType::GROUP:
                decal = ctx->getGroupDecal(element.group);
                break;
            default:
                NPNR_ASSERT_FALSE("Invalid ElementType");
            }
            return decal;
        }
        float distance(Context *ctx, float wx, float wy) const;
    };
    using PickQuadTree = QuadTree<float, PickedElement>;

    Context *ctx_;
    QTimer paintTimer_;
    std::unique_ptr<PeriodicRunner> renderRunner_;

    QPoint lastDragPos_;
    LineShader lineShader_;
    QMatrix4x4 viewMove_;
    float zoom_;

    struct
    {
        QColor background;
        QColor grid;
        QColor frame;
        QColor hidden;
        QColor inactive;
        QColor active;
        QColor selected;
        QColor hovered;
        QColor highlight[8];
    } colors_;

    // Flags that are passed through from renderer arguments to renderer data.
    // These are used by the UI code to signal events that will only fire when
    // the next frame gets rendered.
    struct PassthroughFlags
    {
        bool zoomOutbound;

        PassthroughFlags() :
            zoomOutbound(false) {}
        PassthroughFlags &operator=(const PassthroughFlags &other) noexcept {
            zoomOutbound = other.zoomOutbound;
            return *this;
        }

        void clear()
        {
            zoomOutbound = false;
        }
    };

    struct RendererArgs
    {
        // Decals that he user selected.
        std::vector<DecalXY> selectedDecals;
        // Decals that the user highlighted.
        std::vector<DecalXY> highlightedDecals[8];
        // Decals that the user's mouse is hovering in.
        DecalXY hoveredDecal;
        // Whether to render the above three or skip it.
        bool changed;

        // Flags to pass back into the RendererData.
        PassthroughFlags flags;
    };
    std::unique_ptr<RendererArgs> rendererArgs_;
    QMutex rendererArgsLock_;

    struct RendererData
    {
        LineShaderData gfxByStyle[GraphicElement::STYLE_MAX];
        LineShaderData gfxSelected;
        LineShaderData gfxHovered;
        LineShaderData gfxHighlighted[8];
        // Global bounding box of data from Arch.
        float bbX0, bbY0, bbX1, bbY1;
        // Quadtree for picking objects.
        std::unique_ptr<PickQuadTree> qt;
        // Flags from args.
        PassthroughFlags flags;
    };
    std::unique_ptr<RendererData> rendererData_;
    QMutex rendererDataLock_;

    void zoom(int level);
    void renderLines(void);
    void renderGraphicElement(RendererData *data, LineShaderData &out, const GraphicElement &el, float x, float y);
    void renderDecal(RendererData *data, LineShaderData &out, const DecalXY &decal);
    void renderArchDecal(RendererData *data, const DecalXY &decal);
    void populateQuadTree(RendererData *data, const DecalXY &decal, const PickedElement& element);
    boost::optional<PickedElement> pickElement(float worldx, float worldy);
    QVector4D mouseToWorldCoordinates(int x, int y);
    QVector4D mouseToWorldDimensions(float x, float y);
    QMatrix4x4 getProjection(void);
};

NEXTPNR_NAMESPACE_END

#endif
