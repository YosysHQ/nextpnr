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

#include "lineshader.h"
#include "log.h"

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

    program_->bind();
    attributes_.position = program_->attributeLocation("position");
    attributes_.normal = program_->attributeLocation("normal");
    attributes_.miter = program_->attributeLocation("miter");
    uniforms_.thickness = program_->uniformLocation("thickness");
    uniforms_.projection = program_->uniformLocation("projection");
    uniforms_.color = program_->uniformLocation("color");
    program_->release();

    for (int style = 0; style < GraphicElement::STYLE_MAX; style++) {
        buffers_[style].position = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
        buffers_[style].normal = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
        buffers_[style].miter = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
        buffers_[style].index = QOpenGLBuffer(QOpenGLBuffer::IndexBuffer);

        if (!buffers_[style].vao.create())
            log_abort();
        buffers_[style].vao.bind();

        if (!buffers_[style].position.create())
            log_abort();
        if (!buffers_[style].normal.create())
            log_abort();
        if (!buffers_[style].miter.create())
            log_abort();
        if (!buffers_[style].index.create())
            log_abort();

        buffers_[style].position.setUsagePattern(QOpenGLBuffer::StaticDraw);
        buffers_[style].normal.setUsagePattern(QOpenGLBuffer::StaticDraw);
        buffers_[style].miter.setUsagePattern(QOpenGLBuffer::StaticDraw);
        buffers_[style].index.setUsagePattern(QOpenGLBuffer::StaticDraw);

        buffers_[style].position.bind();
        buffers_[style].normal.bind();
        buffers_[style].miter.bind();
        buffers_[style].index.bind();

        buffers_[style].vao.release();
    }

    return true;
}

void LineShader::update_vbos(enum GraphicElement::style_t style, const LineShaderData &line)
{
    if (buffers_[style].last_vbo_update == line.last_render)
        return;
    buffers_[style].last_vbo_update = line.last_render;

    buffers_[style].indices = line.indices.size();
    if (buffers_[style].indices == 0)
        return;

    buffers_[style].position.bind();
    buffers_[style].position.allocate(&line.vertices[0], sizeof(Vertex2DPOD) * line.vertices.size());

    buffers_[style].normal.bind();
    buffers_[style].normal.allocate(&line.normals[0], sizeof(Vertex2DPOD) * line.normals.size());

    buffers_[style].miter.bind();
    buffers_[style].miter.allocate(&line.miters[0], sizeof(GLfloat) * line.miters.size());

    buffers_[style].index.bind();
    buffers_[style].index.allocate(&line.indices[0], sizeof(GLuint) * line.indices.size());
}

void LineShader::draw(enum GraphicElement::style_t style, const QColor &color, float thickness,
                      const QMatrix4x4 &projection)
{
    auto gl = QOpenGLContext::currentContext()->functions();
    if (buffers_[style].indices == 0)
        return;
    program_->bind();
    buffers_[style].vao.bind();

    program_->setUniformValue(uniforms_.projection, projection);
    program_->setUniformValue(uniforms_.thickness, thickness);
    program_->setUniformValue(uniforms_.color, color.redF(), color.greenF(), color.blueF(), color.alphaF());

    buffers_[style].position.bind();
    program_->enableAttributeArray(attributes_.position);
    program_->setAttributeBuffer(attributes_.position, GL_FLOAT, 0, 2);

    buffers_[style].normal.bind();
    program_->enableAttributeArray(attributes_.normal);
    program_->setAttributeBuffer(attributes_.normal, GL_FLOAT, 0, 2);

    buffers_[style].miter.bind();
    program_->enableAttributeArray(attributes_.miter);
    program_->setAttributeBuffer(attributes_.miter, GL_FLOAT, 0, 1);

    buffers_[style].index.bind();
    gl->glDrawElements(GL_TRIANGLES, buffers_[style].indices, GL_UNSIGNED_INT, (void *)0);

    program_->disableAttributeArray(attributes_.position);
    program_->disableAttributeArray(attributes_.normal);
    program_->disableAttributeArray(attributes_.miter);

    buffers_[style].vao.release();
    program_->release();
}

NEXTPNR_NAMESPACE_END
