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

#include <cstdio>
#include <math.h>

#include <QApplication>
#include <QCoreApplication>
#include <QMouseEvent>
#include <QWidget>

#include "QtImGui.h"
#include "imgui.h"

#include "fpgaviewwidget.h"
#include "log.h"
#include "mainwindow.h"

NEXTPNR_NAMESPACE_BEGIN

FPGAViewWidget::FPGAViewWidget(QWidget *parent)
        : QOpenGLWidget(parent), ctx_(nullptr), paintTimer_(this), lineShader_(this), zoom_(10.0f),
          rendererArgs_(new FPGAViewWidget::RendererArgs), rendererData_(new FPGAViewWidget::RendererData)
{
    colors_.background = QColor("#000000");
    colors_.grid = QColor("#333");
    colors_.frame = QColor("#808080");
    colors_.hidden = QColor("#606060");
    colors_.inactive = QColor("#303030");
    colors_.active = QColor("#f0f0f0");
    colors_.selected = QColor("#ff6600");
    colors_.hovered = QColor("#906030");
    colors_.highlight[0] = QColor("#6495ed");
    colors_.highlight[1] = QColor("#7fffd4");
    colors_.highlight[2] = QColor("#98fb98");
    colors_.highlight[3] = QColor("#ffd700");
    colors_.highlight[4] = QColor("#cd5c5c");
    colors_.highlight[5] = QColor("#fa8072");
    colors_.highlight[6] = QColor("#ff69b4");
    colors_.highlight[7] = QColor("#da70d6");

    rendererArgs_->changed = false;
    rendererArgs_->gridChanged = false;
    rendererArgs_->zoomOutbound = true;

    auto fmt = format();
    fmt.setMajorVersion(3);
    fmt.setMinorVersion(2);
    setFormat(fmt);

    fmt = format();
    if (fmt.majorVersion() < 3) {
        printf("Could not get OpenGL 3.0 context. Aborting.\n");
        log_abort();
    }
    if (fmt.minorVersion() < 2) {
        printf("Could not get OpenGL 3.2 context - trying anyway...\n ");
    }

    connect(&paintTimer_, SIGNAL(timeout()), this, SLOT(update()));
    paintTimer_.start(1000 / 20); // paint GL 20 times per second

    renderRunner_ = std::unique_ptr<PeriodicRunner>(new PeriodicRunner(this, [this] { renderLines(); }));
    renderRunner_->start();
    renderRunner_->startTimer(1000 / 2); // render lines 2 times per second
    setMouseTracking(true);
}

FPGAViewWidget::~FPGAViewWidget() {}

void FPGAViewWidget::newContext(Context *ctx)
{
    ctx_ = ctx;
    {
        QMutexLocker lock(&rendererArgsLock_);

        rendererArgs_->gridChanged = true;
    }
    onSelectedArchItem(std::vector<DecalXY>(), false);
    for (int i = 0; i < 8; i++)
        onHighlightGroupChanged(std::vector<DecalXY>(), i);
    {
        QMutexLocker lock(&rendererArgsLock_);
        rendererArgs_->zoomOutbound = true;
    }
    pokeRenderer();
}

QSize FPGAViewWidget::minimumSizeHint() const { return QSize(640, 480); }

QSize FPGAViewWidget::sizeHint() const { return QSize(640, 480); }

void FPGAViewWidget::initializeGL()
{
    if (!lineShader_.compile()) {
        log_error("Could not compile shader.\n");
    }
    initializeOpenGLFunctions();
    QtImGui::initialize(this);
    glClearColor(colors_.background.red() / 255, colors_.background.green() / 255, colors_.background.blue() / 255,
                 0.0);
}

float FPGAViewWidget::PickedElement::distance(Context *ctx, float wx, float wy) const
{
    // Get DecalXY for this element.
    DecalXY dec = decal(ctx);

    // Coordinates within decal.
    float dx = wx - dec.x;
    float dy = wy - dec.y;

    auto graphics = ctx->getDecalGraphics(dec.decal);
    if (graphics.size() == 0)
        return -1;

    // TODO(q3k): For multi-line decals, find intersections and also calculate distance to them.

    // Go over its' GraphicElements, and calculate the distance to them.
    std::vector<float> distances;
    std::transform(graphics.begin(), graphics.end(), std::back_inserter(distances),
                   [&](const GraphicElement &ge) -> float {
                       switch (ge.type) {
                       case GraphicElement::TYPE_BOX: {
                           // If outside the box, return unit distance to closest border.
                           float outside_x = -1, outside_y = -1;
                           if (dx < ge.x1 || dx > ge.x2) {
                               outside_x = std::min(std::abs(dx - ge.x1), std::abs(dx - ge.x2));
                           }
                           if (dy < ge.y1 || dy > ge.y2) {
                               outside_y = std::min(std::abs(dy - ge.y1), std::abs(dy - ge.y2));
                           }
                           if (outside_x != -1 && outside_y != -1)
                               return std::min(outside_x, outside_y);

                           // If in box, return 0.
                           return 0;
                       }
                       case GraphicElement::TYPE_LINE:
                       case GraphicElement::TYPE_ARROW: {
                           // Return somewhat primitively calculated distance to segment.
                           // TODO(q3k): consider coming up with a better algorithm
                           QVector2D w(wx, wy);
                           QVector2D a(ge.x1, ge.y1);
                           QVector2D b(ge.x2, ge.y2);
                           float dw = a.distanceToPoint(w) + b.distanceToPoint(w);
                           float dab = a.distanceToPoint(b);
                           return std::abs(dw - dab) / dab;
                       }
                       default:
                           // Not close to antyhing.
                           return -1;
                       }
                   });

    // Find smallest non -1 distance.
    // Find closest element.
    return *std::min_element(distances.begin(), distances.end(), [&](float a, float b) {
        if (a == -1)
            return false;
        if (b == -1)
            return true;
        return a < b;
    });
}

void FPGAViewWidget::renderGraphicElement(LineShaderData &out, PickQuadTree::BoundingBox &bb, const GraphicElement &el,
                                          float x, float y)
{
    if (el.type == GraphicElement::TYPE_BOX) {
        auto line = PolyLine(true);
        line.point(x + el.x1, y + el.y1);
        line.point(x + el.x2, y + el.y1);
        line.point(x + el.x2, y + el.y2);
        line.point(x + el.x1, y + el.y2);
        line.build(out);

        bb.setX0(std::min(bb.x0(), x + el.x1));
        bb.setY0(std::min(bb.y0(), y + el.y1));
        bb.setX1(std::max(bb.x1(), x + el.x2));
        bb.setY1(std::max(bb.y1(), y + el.y2));
        return;
    }

    if (el.type == GraphicElement::TYPE_LINE || el.type == GraphicElement::TYPE_ARROW) {
        PolyLine(x + el.x1, y + el.y1, x + el.x2, y + el.y2).build(out);
        bb.setX0(std::min(bb.x0(), x + el.x1));
        bb.setY0(std::min(bb.y0(), y + el.y1));
        bb.setX1(std::max(bb.x1(), x + el.x2));
        bb.setY1(std::max(bb.y1(), y + el.y2));
        return;
    }
}

void FPGAViewWidget::renderDecal(LineShaderData &out, PickQuadTree::BoundingBox &bb, const DecalXY &decal)
{
    if (decal.decal == DecalId())
        return;

    float offsetX = decal.x;
    float offsetY = decal.y;

    for (auto &el : ctx_->getDecalGraphics(decal.decal)) {
        renderGraphicElement(out, bb, el, offsetX, offsetY);
    }
}

void FPGAViewWidget::renderArchDecal(LineShaderData out[GraphicElement::STYLE_MAX], PickQuadTree::BoundingBox &bb,
                                     const DecalXY &decal)
{
    float offsetX = decal.x;
    float offsetY = decal.y;

    for (auto &el : ctx_->getDecalGraphics(decal.decal)) {
        switch (el.style) {
        case GraphicElement::STYLE_FRAME:
        case GraphicElement::STYLE_INACTIVE:
        case GraphicElement::STYLE_ACTIVE:
            renderGraphicElement(out[el.style], bb, el, offsetX, offsetY);
            break;
        default:
            break;
        }
    }
}

void FPGAViewWidget::populateQuadTree(RendererData *data, const DecalXY &decal, const PickedElement &element)
{
    float x = decal.x;
    float y = decal.y;

    for (auto &el : ctx_->getDecalGraphics(decal.decal)) {
        if (el.style == GraphicElement::STYLE_HIDDEN || el.style == GraphicElement::STYLE_FRAME) {
            continue;
        }

        bool res = true;
        if (el.type == GraphicElement::TYPE_BOX) {
            // Boxes are bounded by themselves.
            res = data->qt->insert(PickQuadTree::BoundingBox(x + el.x1, y + el.y1, x + el.x2, y + el.y2), element);
        }

        if (el.type == GraphicElement::TYPE_LINE || el.type == GraphicElement::TYPE_ARROW) {
            // Lines are bounded by their AABB slightly enlarged.
            float x0 = x + el.x1;
            float y0 = y + el.y1;
            float x1 = x + el.x2;
            float y1 = y + el.y2;
            if (x1 < x0)
                std::swap(x0, x1);
            if (y1 < y0)
                std::swap(y0, y1);

            x0 -= 0.01;
            y0 -= 0.01;
            x1 += 0.01;
            y1 += 0.01;

            res = data->qt->insert(PickQuadTree::BoundingBox(x0, y0, x1, y1), element);
        }

        if (!res) {
            NPNR_ASSERT_FALSE("populateQuadTree: could not insert element");
        }
    }
}

QMatrix4x4 FPGAViewWidget::getProjection(void)
{
    QMatrix4x4 matrix;

    const float aspect = float(width()) / float(height());
    matrix.perspective(90, aspect, zoomNear_ - 0.01f, zoomFar_ + 0.01f);
    return matrix;
}

void FPGAViewWidget::paintGL()
{
    auto gl = QOpenGLContext::currentContext()->functions();
    const qreal retinaScale = devicePixelRatio();
    gl->glViewport(0, 0, width() * retinaScale, height() * retinaScale);
    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    QMatrix4x4 matrix = getProjection();
    matrix.translate(0.0f, 0.0f, -zoom_);

    matrix *= viewMove_;

    // Calculate world thickness to achieve a screen 1px/1.1px line.
    float thick1Px = mouseToWorldDimensions(1, 0).x();
    float thick11Px = mouseToWorldDimensions(1.1, 0).x();
    float thick2Px = mouseToWorldDimensions(2, 0).x();

    {
        QMutexLocker locker(&rendererDataLock_);
        // Must be called from a thread holding the OpenGL context
        update_vbos();
    }

    // Render the grid.
    lineShader_.draw(GraphicElement::STYLE_GRID, colors_.grid, thick1Px, matrix);

    // Render Arch graphics.
    lineShader_.draw(GraphicElement::STYLE_FRAME, colors_.frame, thick11Px, matrix);
    lineShader_.draw(GraphicElement::STYLE_HIDDEN, colors_.hidden, thick11Px, matrix);
    lineShader_.draw(GraphicElement::STYLE_INACTIVE, colors_.inactive, thick11Px, matrix);
    lineShader_.draw(GraphicElement::STYLE_ACTIVE, colors_.active, thick11Px, matrix);

    // Draw highlighted items.
    for (int i = 0; i < 8; i++) {
        GraphicElement::style_t style = (GraphicElement::style_t)(GraphicElement::STYLE_HIGHLIGHTED0 + i);
        lineShader_.draw(style, colors_.highlight[i], thick11Px, matrix);
    }

    lineShader_.draw(GraphicElement::STYLE_SELECTED, colors_.selected, thick11Px, matrix);
    lineShader_.draw(GraphicElement::STYLE_HOVER, colors_.hovered, thick2Px, matrix);

    // Render ImGui
    QtImGui::newFrame();
    QMutexLocker lock(&rendererArgsLock_);
    if (!(rendererArgs_->hoveredDecal == DecalXY()) && rendererArgs_->hintText.size() > 0) {
        ImGui::SetNextWindowPos(ImVec2(rendererArgs_->x, rendererArgs_->y));
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(rendererArgs_->hintText.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::Render();
}

void FPGAViewWidget::pokeRenderer(void) { renderRunner_->poke(); }

void FPGAViewWidget::renderLines(void)
{
    if (ctx_ == nullptr)
        return;

    // Data from Context needed to render all decals.
    std::vector<std::pair<DecalXY, BelId>> belDecals;
    std::vector<std::pair<DecalXY, WireId>> wireDecals;
    std::vector<std::pair<DecalXY, PipId>> pipDecals;
    std::vector<std::pair<DecalXY, GroupId>> groupDecals;
    bool decalsChanged = false;
    {
        // Take the UI/Normal mutex on the Context, copy over all we need as
        // fast as we can.
        std::lock_guard<std::mutex> lock_ui(ctx_->ui_mutex);
        std::lock_guard<std::mutex> lock(ctx_->mutex);

        // For now, collapse any decal changes into change of all decals.
        // TODO(q3k): fix this
        if (ctx_->allUiReload) {
            ctx_->allUiReload = false;
            decalsChanged = true;
        }
        if (ctx_->frameUiReload) {
            ctx_->frameUiReload = false;
            decalsChanged = true;
        }
        if (ctx_->belUiReload.size() > 0) {
            ctx_->belUiReload.clear();
            decalsChanged = true;
        }
        if (ctx_->wireUiReload.size() > 0) {
            ctx_->wireUiReload.clear();
            decalsChanged = true;
        }
        if (ctx_->pipUiReload.size() > 0) {
            ctx_->pipUiReload.clear();
            decalsChanged = true;
        }
        if (ctx_->groupUiReload.size() > 0) {
            ctx_->groupUiReload.clear();
            decalsChanged = true;
        }

        // Local copy of decals, taken as fast as possible to not block the P&R.
        if (decalsChanged) {
            for (auto bel : ctx_->getBels()) {
                belDecals.push_back({ctx_->getBelDecal(bel), bel});
            }
            for (auto wire : ctx_->getWires()) {
                wireDecals.push_back({ctx_->getWireDecal(wire), wire});
            }
            for (auto pip : ctx_->getPips()) {
                pipDecals.push_back({ctx_->getPipDecal(pip), pip});
            }
            for (auto group : ctx_->getGroups()) {
                groupDecals.push_back({ctx_->getGroupDecal(group), group});
            }
        }
    }

    // Arguments from the main UI thread on what we should render.
    std::vector<DecalXY> selectedDecals;
    DecalXY hoveredDecal;
    std::vector<DecalXY> highlightedDecals[8];
    bool highlightedOrSelectedChanged;
    bool gridChanged;
    {
        // Take the renderer arguments lock, copy over all we need.
        QMutexLocker lock(&rendererArgsLock_);

        selectedDecals = rendererArgs_->selectedDecals;
        hoveredDecal = rendererArgs_->hoveredDecal;

        for (int i = 0; i < 8; i++)
            highlightedDecals[i] = rendererArgs_->highlightedDecals[i];

        highlightedOrSelectedChanged = rendererArgs_->changed;
        gridChanged = rendererArgs_->gridChanged;
        rendererArgs_->changed = false;
        rendererArgs_->gridChanged = false;
    }

    // Render decals if necessary.
    if (decalsChanged) {
        int last_render[GraphicElement::STYLE_HIGHLIGHTED0];
        {
            QMutexLocker locker(&rendererDataLock_);
            for (int i = 0; i < GraphicElement::STYLE_HIGHLIGHTED0; i++)
                last_render[i] = rendererData_->gfxByStyle[(enum GraphicElement::style_t)i].last_render;
        }

        auto data = std::unique_ptr<FPGAViewWidget::RendererData>(new FPGAViewWidget::RendererData);
        // Reset bounding box.
        data->bbGlobal.clear();

        // Draw Bels.
        for (auto const &decal : belDecals) {
            renderArchDecal(data->gfxByStyle, data->bbGlobal, decal.first);
        }
        // Draw Wires.
        for (auto const &decal : wireDecals) {
            renderArchDecal(data->gfxByStyle, data->bbGlobal, decal.first);
        }
        // Draw Pips.
        for (auto const &decal : pipDecals) {
            renderArchDecal(data->gfxByStyle, data->bbGlobal, decal.first);
        }
        // Draw Groups.
        for (auto const &decal : groupDecals) {
            renderArchDecal(data->gfxByStyle, data->bbGlobal, decal.first);
        }

        // Bounding box should be calculated by now.
        NPNR_ASSERT(data->bbGlobal.w() != 0);
        NPNR_ASSERT(data->bbGlobal.h() != 0);

        // Enlarge the bounding box slightly for the picking - when we insert
        // elements into it, we enlarge their bounding boxes slightly, so
        // we need to give ourselves some sagery margin here.
        auto bb = data->bbGlobal;
        bb.setX0(bb.x0() - 1);
        bb.setY0(bb.y0() - 1);
        bb.setX1(bb.x1() + 1);
        bb.setY1(bb.y1() + 1);

        // Populate picking quadtree.
        data->qt = std::unique_ptr<PickQuadTree>(new PickQuadTree(bb));
        for (auto const &decal : belDecals) {
            populateQuadTree(data.get(), decal.first,
                             PickedElement::fromBel(decal.second, decal.first.x, decal.first.y));
        }
        for (auto const &decal : wireDecals) {
            populateQuadTree(data.get(), decal.first,
                             PickedElement::fromWire(decal.second, decal.first.x, decal.first.y));
        }
        for (auto const &decal : pipDecals) {
            populateQuadTree(data.get(), decal.first,
                             PickedElement::fromPip(decal.second, decal.first.x, decal.first.y));
        }
        for (auto const &decal : groupDecals) {
            populateQuadTree(data.get(), decal.first,
                             PickedElement::fromGroup(decal.second, decal.first.x, decal.first.y));
        }

        // Swap over.
        {
            QMutexLocker lock(&rendererDataLock_);

            // If we're not re-rendering any highlights/selections, let's
            // copy them over from teh current object.
            data->gfxGrid = rendererData_->gfxGrid;
            if (!highlightedOrSelectedChanged) {
                data->gfxSelected = rendererData_->gfxSelected;
                data->gfxHovered = rendererData_->gfxHovered;
                for (int i = 0; i < 8; i++)
                    data->gfxHighlighted[i] = rendererData_->gfxHighlighted[i];
            }
            for (int i = 0; i < GraphicElement::STYLE_HIGHLIGHTED0; i++)
                data->gfxByStyle[(enum GraphicElement::style_t)i].last_render = ++last_render[i];
            rendererData_ = std::move(data);
        }
    }
    if (gridChanged) {
        QMutexLocker locker(&rendererDataLock_);
        rendererData_->gfxGrid.clear();
        // Render grid.
        for (float i = 0.0f; i < 1.0f * ctx_->getGridDimX() + 1; i += 1.0f) {
            PolyLine(i, 0.0f, i, 1.0f * ctx_->getGridDimY()).build(rendererData_->gfxGrid);
        }
        for (float i = 0.0f; i < 1.0f * ctx_->getGridDimY() + 1; i += 1.0f) {
            PolyLine(0.0f, i, 1.0f * ctx_->getGridDimX(), i).build(rendererData_->gfxGrid);
        }
        rendererData_->gfxGrid.last_render++;
    }
    if (highlightedOrSelectedChanged) {
        QMutexLocker locker(&rendererDataLock_);

        // Whether the currently being hovered decal is also selected.
        bool hoveringSelected = false;
        // Render selected.
        rendererData_->bbSelected.clear();
        rendererData_->gfxSelected.clear();
        for (auto &decal : selectedDecals) {
            if (decal == hoveredDecal)
                hoveringSelected = true;
            renderDecal(rendererData_->gfxSelected, rendererData_->bbSelected, decal);
        }
        rendererData_->gfxSelected.last_render++;

        // Render hovered.
        rendererData_->gfxHovered.clear();
        if (!hoveringSelected) {
            renderDecal(rendererData_->gfxHovered, rendererData_->bbGlobal, hoveredDecal);
        }
        rendererData_->gfxHovered.last_render++;

        // Render highlighted.
        for (int i = 0; i < 8; i++) {
            rendererData_->gfxHighlighted[i].clear();
            for (auto &decal : highlightedDecals[i]) {
                renderDecal(rendererData_->gfxHighlighted[i], rendererData_->bbGlobal, decal);
            }
            rendererData_->gfxHighlighted[i].last_render++;
        }
    }

    {
        QMutexLocker lock(&rendererArgsLock_);

        if (rendererArgs_->zoomOutbound) {
            zoomOutbound();
            rendererArgs_->zoomOutbound = false;
        }
    }
}

void FPGAViewWidget::onSelectedArchItem(std::vector<DecalXY> decals, bool keep)
{
    {
        QMutexLocker locker(&rendererArgsLock_);
        if (keep) {
            std::copy(decals.begin(), decals.end(), std::back_inserter(rendererArgs_->selectedDecals));
        } else {
            rendererArgs_->selectedDecals = decals;
        }
        rendererArgs_->changed = true;
    }
    pokeRenderer();
}

void FPGAViewWidget::onHighlightGroupChanged(std::vector<DecalXY> decals, int group)
{
    {
        QMutexLocker locker(&rendererArgsLock_);
        rendererArgs_->highlightedDecals[group] = decals;
        rendererArgs_->changed = true;
    }
    pokeRenderer();
}

void FPGAViewWidget::onHoverItemChanged(DecalXY decal)
{
    QMutexLocker locked(&rendererArgsLock_);
    rendererArgs_->hoveredDecal = decal;
    rendererArgs_->changed = true;
    pokeRenderer();
}

void FPGAViewWidget::resizeGL(int width, int height) {}

boost::optional<FPGAViewWidget::PickedElement> FPGAViewWidget::pickElement(float worldx, float worldy)
{
    // Get elements from renderer whose BBs correspond to the pick.
    std::vector<PickedElement> elems;
    {
        QMutexLocker locker(&rendererDataLock_);
        if (rendererData_->qt == nullptr) {
            return {};
        }
        elems = rendererData_->qt->get(worldx, worldy);
    }

    if (elems.size() == 0) {
        return {};
    }

    // Calculate distances to all elements picked.
    using ElemDist = std::pair<const PickedElement *, float>;
    std::vector<ElemDist> distances;
    std::transform(elems.begin(), elems.end(), std::back_inserter(distances), [&](const PickedElement &e) -> ElemDist {
        return std::make_pair(&e, e.distance(ctx_, worldx, worldy));
    });

    // Find closest non -1 element.
    auto closest = std::min_element(distances.begin(), distances.end(), [&](const ElemDist &a, const ElemDist &b) {
        if (a.second == -1)
            return false;
        if (b.second == -1)
            return true;
        return a.second < b.second;
    });

    // All out of reach?
    if (closest->second < 0) {
        return {};
    }

    return *(closest->first);
}

void FPGAViewWidget::mousePressEvent(QMouseEvent *event)
{
    ImGuiIO &io = ImGui::GetIO();
    if (io.WantCaptureMouse)
        return;

    if (event->buttons() & Qt::RightButton || event->buttons() & Qt::MidButton) {
        lastDragPos_ = event->pos();
    }
    if (event->buttons() & Qt::LeftButton) {
        bool ctrl = QApplication::keyboardModifiers().testFlag(Qt::ControlModifier);

        auto world = mouseToWorldCoordinates(event->x(), event->y());
        auto closestOr = pickElement(world.x(), world.y());
        if (!closestOr) {
            // If we clicked on empty space and aren't holding down ctrl,
            // clear the selection.
            if (!ctrl) {
                QMutexLocker locked(&rendererArgsLock_);
                rendererArgs_->selectedDecals.clear();
                rendererArgs_->changed = true;
                pokeRenderer();
            }
            return;
        }

        auto closest = closestOr.value();
        if (closest.type == ElementType::BEL) {
            clickedBel(closest.bel, ctrl);
        } else if (closest.type == ElementType::WIRE) {
            clickedWire(closest.wire, ctrl);
        } else if (closest.type == ElementType::PIP) {
            clickedPip(closest.pip, ctrl);
        }
    }
}

void FPGAViewWidget::mouseMoveEvent(QMouseEvent *event)
{
    ImGuiIO &io = ImGui::GetIO();
    if (io.WantCaptureMouse)
        return;

    if (event->buttons() & Qt::RightButton || event->buttons() & Qt::MidButton) {
        const int dx = event->x() - lastDragPos_.x();
        const int dy = event->y() - lastDragPos_.y();
        lastDragPos_ = event->pos();

        auto world = mouseToWorldDimensions(dx, dy);
        viewMove_.translate(world.x(), -world.y());

        update();
        return;
    }

    auto world = mouseToWorldCoordinates(event->x(), event->y());
    auto closestOr = pickElement(world.x(), world.y());
    // No elements? No decal.
    if (!closestOr) {
        QMutexLocker locked(&rendererArgsLock_);
        rendererArgs_->hoveredDecal = DecalXY();
        rendererArgs_->changed = true;
        rendererArgs_->hintText = "";
        pokeRenderer();
        return;
    }

    auto closest = closestOr.value();

    {
        QMutexLocker locked(&rendererArgsLock_);
        rendererArgs_->hoveredDecal = closest.decal(ctx_);
        rendererArgs_->changed = true;
        rendererArgs_->x = event->x();
        rendererArgs_->y = event->y();
        if (closest.type == ElementType::BEL) {
            rendererArgs_->hintText = std::string("BEL\n") + ctx_->getBelName(closest.bel).c_str(ctx_);
            CellInfo *cell = ctx_->getBoundBelCell(closest.bel);
            if (cell != nullptr)
                rendererArgs_->hintText += std::string("\nCELL\n") + ctx_->nameOf(cell);
        } else if (closest.type == ElementType::WIRE) {
            rendererArgs_->hintText = std::string("WIRE\n") + ctx_->getWireName(closest.wire).c_str(ctx_);
            NetInfo *net = ctx_->getBoundWireNet(closest.wire);
            if (net != nullptr)
                rendererArgs_->hintText += std::string("\nNET\n") + ctx_->nameOf(net);
        } else if (closest.type == ElementType::PIP) {
            rendererArgs_->hintText = std::string("PIP\n") + ctx_->getPipName(closest.pip).c_str(ctx_);
            NetInfo *net = ctx_->getBoundPipNet(closest.pip);
            if (net != nullptr)
                rendererArgs_->hintText += std::string("\nNET\n") + ctx_->nameOf(net);
        } else if (closest.type == ElementType::GROUP) {
            rendererArgs_->hintText = std::string("GROUP\n") + ctx_->getGroupName(closest.group).c_str(ctx_);
        } else
            rendererArgs_->hintText = "";

        pokeRenderer();
    }
    update();
}

// Invert the projection matrix to calculate screen/mouse to world/grid
// coordinates.
QVector4D FPGAViewWidget::mouseToWorldCoordinates(int x, int y)
{
    const qreal retinaScale = devicePixelRatio();

    auto projection = getProjection();

    QMatrix4x4 vp;
    vp.viewport(0, 0, width() * retinaScale, height() * retinaScale);

    QVector4D vec(x, y, 1, 1);
    vec = vp.inverted() * vec;
    vec = projection.inverted() * QVector4D(vec.x(), vec.y(), -1, 1);

    // Hic sunt dracones.
    // TODO(q3k): grab a book, remind yourselfl linear algebra and undo this
    // operation properly.
    QVector3D ray = vec.toVector3DAffine();
    ray.normalize();
    ray.setX((ray.x() / -ray.z()) * zoom_);
    ray.setY((ray.y() / ray.z()) * zoom_);
    ray.setZ(1.0);

    vec = viewMove_.inverted() * QVector4D(ray.x(), ray.y(), ray.z(), 1.0);
    vec.setZ(0);

    return vec;
}

QVector4D FPGAViewWidget::mouseToWorldDimensions(float x, float y)
{
    QMatrix4x4 p = getProjection();
    p.translate(0.0f, 0.0f, -zoom_);
    QVector2D unit = p.map(QVector4D(1, 1, 0, 1)).toVector2DAffine();

    float sx = (((float)x) / (width() / 2));
    float sy = (((float)y) / (height() / 2));
    return QVector4D(sx / unit.x(), sy / unit.y(), 0, 1);
}

void FPGAViewWidget::wheelEvent(QWheelEvent *event)
{
    ImGuiIO &io = ImGui::GetIO();
    if (io.WantCaptureMouse)
        return;

    QPoint degree = event->angleDelta() / 8;

    if (!degree.isNull())
        zoom(degree.y());
}

void FPGAViewWidget::zoom(int level)
{
    if (zoom_ < zoomLvl1_) {
        zoom_ -= level / 500.0;
    } else if (zoom_ < zoomLvl2_) {
        zoom_ -= level / 100.0;
    } else {
        zoom_ -= level / 10.0;
    }

    if (zoom_ < zoomNear_)
        zoom_ = zoomNear_;
    else if (zoom_ > zoomFar_)
        zoom_ = zoomFar_;
    update();
}

void FPGAViewWidget::clampZoom()
{
    if (zoom_ < zoomNear_)
        zoom_ = zoomNear_;
    else if (zoom_ > zoomFar_)
        zoom_ = zoomFar_;
}

void FPGAViewWidget::zoomIn() { zoom(10); }

void FPGAViewWidget::zoomOut() { zoom(-10); }

void FPGAViewWidget::zoomToBB(const PickQuadTree::BoundingBox &bb, float margin, bool clamp)
{
    if (fabs(bb.w()) < 0.00005 && fabs(bb.h()) < 0.00005)
        return;

    viewMove_.setToIdentity();
    viewMove_.translate(-(bb.x0() + bb.w() / 2), -(bb.y0() + bb.h() / 2));

    // Our FOV is π/2, so distance for camera to see a plane of width H is H/2.
    // We add 1 unit to cover half a unit of extra space around.
    float distance_w = bb.w() / 2 + margin;
    float distance_h = bb.h() / 2 + margin;
    zoom_ = std::max(distance_w, distance_h);
    if (clamp)
        clampZoom();
}

void FPGAViewWidget::zoomSelected()
{
    {
        QMutexLocker lock(&rendererDataLock_);
        if (rendererData_->bbSelected.x0() != std::numeric_limits<float>::infinity())
            zoomToBB(rendererData_->bbSelected, 0.5f, true);
    }
    update();
}

void FPGAViewWidget::zoomOutbound()
{
    {
        QMutexLocker lock(&rendererDataLock_);
        zoomToBB(rendererData_->bbGlobal, 1.0f, false);
        zoomFar_ = zoom_;
    }
}

void FPGAViewWidget::leaveEvent(QEvent *event)
{
    QMutexLocker locked(&rendererArgsLock_);
    rendererArgs_->hoveredDecal = DecalXY();
    rendererArgs_->changed = true;
    rendererArgs_->hintText = "";
    pokeRenderer();
}

void FPGAViewWidget::update_vbos()
{
    lineShader_.update_vbos(GraphicElement::STYLE_GRID, rendererData_->gfxGrid);

    for (int style = GraphicElement::STYLE_FRAME; style < GraphicElement::STYLE_HIGHLIGHTED0; style++) {
        lineShader_.update_vbos((enum GraphicElement::style_t)(style), rendererData_->gfxByStyle[style]);
    }

    for (int i = 0; i < 8; i++) {
        GraphicElement::style_t style = (GraphicElement::style_t)(GraphicElement::STYLE_HIGHLIGHTED0 + i);
        lineShader_.update_vbos(style, rendererData_->gfxHighlighted[i]);
    }

    lineShader_.update_vbos(GraphicElement::STYLE_SELECTED, rendererData_->gfxSelected);
    lineShader_.update_vbos(GraphicElement::STYLE_HOVER, rendererData_->gfxHovered);
}

NEXTPNR_NAMESPACE_END
