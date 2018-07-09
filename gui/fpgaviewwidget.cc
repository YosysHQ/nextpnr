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

#include "fpgaviewwidget.h"
#include "log.h"
#include "mainwindow.h"

NEXTPNR_NAMESPACE_BEGIN

void PolyLine::buildPoint(LineShaderData *building, const QVector2D *prev, const QVector2D *cur,
                          const QVector2D *next) const
{
    // buildPoint emits two vertices per line point, along with normals to move
    // them the right directio when rendering and miter to compensate for
    // bends.

    if (cur == nullptr) {
        // BUG
        return;
    }

    if (prev == nullptr && next == nullptr) {
        // BUG
        return;
    }

    // TODO(q3k): fast path for vertical/horizontal lines?

    // TODO(q3k): consider moving some of the linear algebra to the GPU,
    // they're better at this than poor old CPUs.

    // Build two unit vectors pointing in the direction of the two segments
    // defined by (prev, cur) and (cur, next)
    QVector2D dprev, dnext;
    if (prev == nullptr) {
        dnext = *next - *cur;
        dprev = dnext;
    } else if (next == nullptr) {
        dprev = *cur - *prev;
        dnext = dprev;
    } else {
        dprev = *cur - *prev;
        dnext = *next - *cur;
    }
    dprev.normalize();
    dnext.normalize();

    // Calculate tangent unit vector.
    QVector2D tangent(dprev + dnext);
    tangent.normalize();

    // Calculate normal to tangent - this is the line on which the vectors need
    // to be pushed to build a thickened line.
    const QVector2D tangent_normal = QVector2D(-tangent.y(), tangent.x());

    // Calculate normal to one of the lines.
    const QVector2D dprev_normal = QVector2D(-dprev.y(), dprev.x());
    // https://people.eecs.berkeley.edu/~sequin/CS184/IMGS/Sweep_PolyLine.jpg
    // (the ^-1 is performed in the shader)
    const float miter = QVector2D::dotProduct(tangent_normal, dprev_normal);

    const float x = cur->x();
    const float y = cur->y();
    const float mx = tangent_normal.x();
    const float my = tangent_normal.y();

    // Push back 'left' vertex.
    building->vertices.push_back(Vertex2DPOD(x, y));
    building->normals.push_back(Vertex2DPOD(mx, my));
    building->miters.push_back(miter);

    // Push back 'right' vertex.
    building->vertices.push_back(Vertex2DPOD(x, y));
    building->normals.push_back(Vertex2DPOD(mx, my));
    building->miters.push_back(-miter);
}

void PolyLine::build(LineShaderData &target) const
{
    if (points_.size() < 2) {
        return;
    }
    const QVector2D *first = &points_.front();
    const QVector2D *last = &points_.back();

    // Index number of vertices, used to build the index buffer.
    unsigned int startIndex = target.vertices.size();
    unsigned int index = startIndex;

    // For every point on the line, call buildPoint with (prev, point, next).
    // If we're building a closed line, prev/next wrap around. Otherwise
    // they are passed as nullptr and buildPoint interprets that accordinglu.
    const QVector2D *prev = nullptr;

    // Loop iterator used to ensure next is valid.
    unsigned int i = 0;
    for (const QVector2D &point : points_) {
        const QVector2D *next = nullptr;
        if (++i < points_.size()) {
            next = (&point + 1);
        }

        // If the line is closed, wrap around. Otherwise, pass nullptr.
        if (prev == nullptr && closed_) {
            buildPoint(&target, last, &point, next);
        } else if (next == nullptr && closed_) {
            buildPoint(&target, prev, &point, first);
        } else {
            buildPoint(&target, prev, &point, next);
        }

        // If we have a prev point relative to cur, build a pair of triangles
        // to render vertices into lines.
        if (prev != nullptr) {
            target.indices.push_back(index);
            target.indices.push_back(index + 1);
            target.indices.push_back(index + 2);

            target.indices.push_back(index + 2);
            target.indices.push_back(index + 1);
            target.indices.push_back(index + 3);

            index += 2;
        }
        prev = &point;
    }

    // If we're closed, build two more vertices that loop the line around.
    if (closed_) {
        target.indices.push_back(index);
        target.indices.push_back(index + 1);
        target.indices.push_back(startIndex);

        target.indices.push_back(startIndex);
        target.indices.push_back(index + 1);
        target.indices.push_back(startIndex + 1);
    }
}

bool LineShader::compile(void)
{
    program_ = new QOpenGLShaderProgram(parent_);
    program_->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource_);
    program_->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource_);
    if (!program_->link()) {
        printf("could not link program: %s\n", program_->log().toStdString().c_str());
        return false;
    }

    if (!vao_.create())
        log_abort();
    vao_.bind();

    if (!buffers_.position.create())
        log_abort();
    if (!buffers_.normal.create())
        log_abort();
    if (!buffers_.miter.create())
        log_abort();
    if (!buffers_.index.create())
        log_abort();

    attributes_.position = program_->attributeLocation("position");
    attributes_.normal = program_->attributeLocation("normal");
    attributes_.miter = program_->attributeLocation("miter");
    uniforms_.thickness = program_->uniformLocation("thickness");
    uniforms_.projection = program_->uniformLocation("projection");
    uniforms_.color = program_->uniformLocation("color");

    vao_.release();
    return true;
}

void LineShader::draw(const LineShaderData &line, const QMatrix4x4 &projection)
{
    auto gl = QOpenGLContext::currentContext()->functions();
    vao_.bind();
    program_->bind();

    buffers_.position.bind();
    buffers_.position.allocate(&line.vertices[0], sizeof(Vertex2DPOD) * line.vertices.size());

    buffers_.normal.bind();
    buffers_.normal.allocate(&line.normals[0], sizeof(Vertex2DPOD) * line.normals.size());

    buffers_.miter.bind();
    buffers_.miter.allocate(&line.miters[0], sizeof(GLfloat) * line.miters.size());

    buffers_.index.bind();
    buffers_.index.allocate(&line.indices[0], sizeof(GLuint) * line.indices.size());

    program_->setUniformValue(uniforms_.projection, projection);
    program_->setUniformValue(uniforms_.thickness, line.thickness);
    program_->setUniformValue(uniforms_.color, line.color.r, line.color.g, line.color.b, line.color.a);

    buffers_.position.bind();
    program_->enableAttributeArray("position");
    gl->glVertexAttribPointer(attributes_.position, 2, GL_FLOAT, GL_FALSE, 0, (void *)0);

    buffers_.normal.bind();
    program_->enableAttributeArray("normal");
    gl->glVertexAttribPointer(attributes_.normal, 2, GL_FLOAT, GL_FALSE, 0, (void *)0);

    buffers_.miter.bind();
    program_->enableAttributeArray("miter");
    gl->glVertexAttribPointer(attributes_.miter, 1, GL_FLOAT, GL_FALSE, 0, (void *)0);

    buffers_.index.bind();
    gl->glDrawElements(GL_TRIANGLES, line.indices.size(), GL_UNSIGNED_INT, (void *)0);

    program_->disableAttributeArray("miter");
    program_->disableAttributeArray("normal");
    program_->disableAttributeArray("position");

    program_->release();
    vao_.release();
}

FPGAViewWidget::FPGAViewWidget(QWidget *parent)
        : QOpenGLWidget(parent), moveX_(0), moveY_(0), zoom_(10.0f), lineShader_(this), ctx_(nullptr)
{
    auto fmt = format();
    fmt.setMajorVersion(3);
    fmt.setMinorVersion(1);
    setFormat(fmt);

    fmt = format();
    // printf("FPGAViewWidget running on OpenGL %d.%d\n", fmt.majorVersion(), fmt.minorVersion());
    if (fmt.majorVersion() < 3) {
        printf("Could not get OpenGL 3.0 context. Aborting.\n");
        log_abort();
    }
    if (fmt.minorVersion() < 1) {
        printf("Could not get OpenGL 3.1 context - trying anyway...\n ");
    }
}

FPGAViewWidget::~FPGAViewWidget() {}

void FPGAViewWidget::newContext(Context *ctx)
{
    ctx_ = ctx;
    update();
}

QSize FPGAViewWidget::minimumSizeHint() const { return QSize(640, 480); }

QSize FPGAViewWidget::sizeHint() const { return QSize(640, 480); }

void FPGAViewWidget::setXTranslation(float t_x)
{
    if (t_x == moveX_)
        return;

    moveX_ = t_x;
    update();
}

void FPGAViewWidget::setYTranslation(float t_y)
{
    if (t_y == moveY_)
        return;

    moveY_ = t_y;
    update();
}

void FPGAViewWidget::setZoom(float t_z)
{
    if (t_z == zoom_)
        return;
    zoom_ = t_z;

    if (zoom_ < 1.0f)
        zoom_ = 1.0f;
    if (zoom_ > 500.f)
        zoom_ = 500.0f;

    update();
}

void FPGAViewWidget::initializeGL()
{
    if (!lineShader_.compile()) {
        log_error("Could not compile shader.\n");
    }
    initializeOpenGLFunctions();
    glClearColor(1.0, 1.0, 1.0, 0.0);
}

void FPGAViewWidget::drawElement(LineShaderData &out, const GraphicElement &el)
{
    const float scale = 1.0, offset = 0.0;

    if (el.type == GraphicElement::G_BOX) {
        auto line = PolyLine(true);
        line.point(offset + scale * el.x1, offset + scale * el.y1);
        line.point(offset + scale * el.x2, offset + scale * el.y1);
        line.point(offset + scale * el.x2, offset + scale * el.y2);
        line.point(offset + scale * el.x1, offset + scale * el.y2);
        line.build(out);
    }

    if (el.type == GraphicElement::G_LINE) {
        PolyLine(offset + scale * el.x1, offset + scale * el.y1, offset + scale * el.x2, offset + scale * el.y2)
                .build(out);
    }
}

void FPGAViewWidget::paintGL()
{
    auto gl = QOpenGLContext::currentContext()->functions();
    const qreal retinaScale = devicePixelRatio();
    gl->glViewport(0, 0, width() * retinaScale, height() * retinaScale);
    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const float aspect = float(width()) / float(height());

    QMatrix4x4 matrix;
    matrix.ortho(QRectF(-aspect / 2.0, -0.5, aspect, 1.0f));
    matrix.translate(moveX_, moveY_, -0.5);
    matrix.scale(zoom_ * 0.01f, zoom_ * 0.01f, 0);

    // Draw grid.
    auto grid = LineShaderData(0.001f, QColor("#DDD"));
    for (float i = -100.0f; i < 100.0f; i += 1.0f) {
        PolyLine(-100.0f, i, 100.0f, i).build(grid);
        PolyLine(i, -100.0f, i, 100.0f).build(grid);
    }
    lineShader_.draw(grid, matrix);

    // Draw Bels.
    auto bels = LineShaderData(0.0005f, QColor("#b000ba"));
    if (ctx_) {
        for (auto bel : ctx_->getBels()) {
            for (auto &el : ctx_->getBelGraphics(bel))
                drawElement(bels, el);
        }
        lineShader_.draw(bels, matrix);
    }

    // Draw Frame Graphics.
    auto frames = LineShaderData(0.002f, QColor("#0066ba"));
    if (ctx_) {
        for (auto &el : ctx_->getFrameGraphics()) {
            drawElement(frames, el);
        }
        lineShader_.draw(frames, matrix);
    }
}

void FPGAViewWidget::resizeGL(int width, int height) {}

void FPGAViewWidget::mousePressEvent(QMouseEvent *event)
{
    startDragX_ = moveX_;
    startDragY_ = moveY_;
    lastPos_ = event->pos();
}

void FPGAViewWidget::mouseMoveEvent(QMouseEvent *event)
{
    const int dx = event->x() - lastPos_.x();
    const int dy = event->y() - lastPos_.y();

    const qreal retinaScale = devicePixelRatio();
    float aspect = float(width()) / float(height());
    const float dx_scale = dx * (1 / (float)width() * retinaScale * aspect);
    const float dy_scale = dy * (1 / (float)height() * retinaScale);

    float xpos = dx_scale + startDragX_;
    float ypos = dy_scale + startDragY_;

    setXTranslation(xpos);
    setYTranslation(ypos);
}

void FPGAViewWidget::wheelEvent(QWheelEvent *event)
{
    QPoint degree = event->angleDelta() / 8;

    if (!degree.isNull()) {
        float steps = degree.y() / 15.0;
        setZoom(zoom_ + steps);
    }
}

NEXTPNR_NAMESPACE_END
