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
#include "lineshader.h"

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

  private:
    const float zoomNear_ = 1.0f;    // do not zoom closer than this
    const float zoomFar_ = 10000.0f; // do not zoom further than this
    const float zoomLvl1_ = 100.0f;
    const float zoomLvl2_ = 50.0f;

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
        QColor highlight[8];
    } colors_;

    struct RendererData
    {
        LineShaderData gfxByStyle[GraphicElement::STYLE_MAX];
        LineShaderData gfxSelected;
        LineShaderData gfxHighlighted[8];
    };
    std::unique_ptr<RendererData> rendererData_;
    QMutex rendererDataLock_;

    struct RendererArgs
    {
        std::vector<DecalXY> selectedDecals;
        std::vector<DecalXY> highlightedDecals[8];
        bool highlightedOrSelectedChanged;
    };
    std::unique_ptr<RendererArgs> rendererArgs_;
    QMutex rendererArgsLock_;

    void zoom(int level);
    void renderLines(void);
    void drawGraphicElement(LineShaderData &out, const GraphicElement &el, float x, float y);
    void drawDecal(LineShaderData &out, const DecalXY &decal);
    void drawArchDecal(LineShaderData out[GraphicElement::STYLE_MAX], const DecalXY &decal);
    QVector4D mouseToWorldCoordinates(int x, int y);
    QMatrix4x4 getProjection(void);
};

NEXTPNR_NAMESPACE_END

#endif
