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
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPainter>

#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

// Vertex2DPOD is a structure of X, Y coordinates that can be passed to OpenGL
// directly.
NPNR_PACKED_STRUCT(struct Vertex2DPOD {
    GLfloat x;
    GLfloat y;

    Vertex2DPOD(GLfloat X, GLfloat Y) : x(X), y(Y) {}
});

// LineShaderData is a built set of vertices that can be rendered by the
// LineShader.
// Each LineShaderData can have its' own color and thickness.
struct LineShaderData
{
    std::vector<Vertex2DPOD> vertices;
    std::vector<Vertex2DPOD> normals;
    std::vector<GLfloat> miters;
    std::vector<GLuint> indices;

    LineShaderData(void) {}

    void clear(void)
    {
        vertices.clear();
        normals.clear();
        miters.clear();
        indices.clear();
    }
};

// PolyLine is a set of segments defined by points, that can be built to a
// ShaderLine for GPU rendering.
class PolyLine
{
  private:
    std::vector<QVector2D> points_;
    bool closed_;

    void buildPoint(LineShaderData *building, const QVector2D *prev, const QVector2D *cur, const QVector2D *next) const;

  public:
    // Create an empty PolyLine.
    PolyLine(bool closed = false) : closed_(closed) {}

    // Create a non-closed polyline consisting of one segment.
    PolyLine(float x0, float y0, float x1, float y1) : closed_(false)
    {
        point(x0, y0);
        point(x1, y1);
    }

    // Add a point to the PolyLine.
    void point(float x, float y) { points_.push_back(QVector2D(x, y)); }

    // Built PolyLine to shader data.
    void build(LineShaderData &target) const;

    // Set whether line is closed (ie. a loop).
    void setClosed(bool closed) { closed_ = closed; }
};

// LineShader is an OpenGL shader program that renders LineShaderData on the
// GPU.
// The LineShader expects two vertices per line point. It will push those
// vertices along the given normal * miter. This is used to 'stretch' the line
// to be as wide as the given thickness. The normal and miter are calculated
// by the PolyLine build method in order to construct a constant thickness line
// with miter edge joints.
//
//        +------+------+
//
//               |
//        PolyLine.build()
//               |
//               V
//
//        ^      ^      ^
//        |      |      |    <--- normal vectors (x2, pointing in the same
//       +/+----+/+----+/+        direction)
//
//               |
//         vertex shader
//               |
//               V
//
//        +------+------+ ^ by normal * miter * thickness/2
//        |      |      |
//        +------+------+ V by normal * miter * thickness/2
//
//                         (miter is flipped for every second vertex generated)
class LineShader
{
  private:
    QObject *parent_;
    QOpenGLShaderProgram *program_;

    // GL attribute locations.
    struct
    {
        // original position of line vertex
        GLuint position;
        // normal by which vertex should be translated
        GLuint normal;
        // scalar defining:
        // - how stretched the normal vector should be to
        //   compensate for bends
        // - which way the normal should be applied (+1 for one vertex, -1
        //   for the other)
        GLuint miter;
    } attributes_;

    // GL buffers
    struct
    {
        QOpenGLBuffer position;
        QOpenGLBuffer normal;
        QOpenGLBuffer miter;
        QOpenGLBuffer index;
    } buffers_;

    // GL uniform locations.
    struct
    {
        // combines m/v/p matrix to apply
        GLuint projection;
        // desired thickness of line
        GLuint thickness;
        // color of line
        GLuint color;
    } uniforms_;

    QOpenGLVertexArrayObject vao_;

  public:
    LineShader(QObject *parent) : parent_(parent), program_(nullptr)
    {
        buffers_.position = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
        buffers_.position.setUsagePattern(QOpenGLBuffer::StaticDraw);

        buffers_.normal = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
        buffers_.normal.setUsagePattern(QOpenGLBuffer::StaticDraw);

        buffers_.miter = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
        buffers_.miter.setUsagePattern(QOpenGLBuffer::StaticDraw);

        buffers_.index = QOpenGLBuffer(QOpenGLBuffer::IndexBuffer);
        buffers_.index.setUsagePattern(QOpenGLBuffer::StaticDraw);
    }

    static constexpr const char *vertexShaderSource_ =
            "#version 110\n"
            "attribute highp vec2  position;\n"
            "attribute highp vec2  normal;\n"
            "attribute highp float miter;\n"
            "uniform   highp float thickness;\n"
            "uniform   highp mat4  projection;\n"
            "void main() {\n"
            "   vec2 p = position.xy + vec2(normal * thickness/2.0 / miter);\n"
            "   gl_Position = projection * vec4(p, 0.0, 1.0);\n"
            "}\n";

    static constexpr const char *fragmentShaderSource_ = "#version 110\n"
                                                         "uniform   lowp  vec4  color;\n"
                                                         "void main() {\n"
                                                         "   gl_FragColor = color;\n"
                                                         "}\n";

    // Must be called on initialization.
    bool compile(void);

    // Render a LineShaderData with a given M/V/P transformation.
    void draw(const LineShaderData &data, const QColor &color, float thickness, const QMatrix4x4 &projection);
};

class FPGAViewWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT
    Q_PROPERTY(QColor backgroundColor MEMBER backgroundColor_ DESIGNABLE true)
    Q_PROPERTY(QColor gridColor MEMBER gridColor_ DESIGNABLE true)
    Q_PROPERTY(QColor gFrameColor MEMBER gFrameColor_ DESIGNABLE true)
    Q_PROPERTY(QColor gHiddenColor MEMBER gHiddenColor_ DESIGNABLE true)
    Q_PROPERTY(QColor gInactiveColor MEMBER gInactiveColor_ DESIGNABLE true)
    Q_PROPERTY(QColor gActiveColor MEMBER gActiveColor_ DESIGNABLE true)
    Q_PROPERTY(QColor gSelectedColor MEMBER gSelectedColor_ DESIGNABLE true)
    Q_PROPERTY(QColor frameColor MEMBER frameColor_ DESIGNABLE true)

  public:
    FPGAViewWidget(QWidget *parent = 0);
    ~FPGAViewWidget();

    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

    void setXTranslation(float t_x);
    void setYTranslation(float t_y);
    void setZoom(float t_z);

    void xRotationChanged(int angle);
    void yRotationChanged(int angle);
    void zRotationChanged(int angle);

  protected:
    void initializeGL() Q_DECL_OVERRIDE;
    void paintGL() Q_DECL_OVERRIDE;
    void resizeGL(int width, int height) Q_DECL_OVERRIDE;
    void mousePressEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    void mouseMoveEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    void wheelEvent(QWheelEvent *event) Q_DECL_OVERRIDE;
    void drawDecal(LineShaderData &data, const DecalXY &decal);
    void drawDecal(LineShaderData out[], const DecalXY &decal);
  public Q_SLOTS:
    void newContext(Context *ctx);
    void onSelectedArchItem(std::vector<DecalXY> decals);
    void onHighlightGroupChanged(std::vector<DecalXY> decals, int group);

  private:
    QPoint lastPos_;
    LineShader lineShader_;
    QMatrix4x4 viewMove_;
    float zoom_;
    QMatrix4x4 getProjection(void);
    QVector4D mouseToWorldCoordinates(int x, int y);

    const float zoomNear_ = 1.0f;    // do not zoom closer than this
    const float zoomFar_ = 10000.0f; // do not zoom further than this

    const float zoomLvl1_ = 100.0f;
    const float zoomLvl2_ = 50.0f;

    Context *ctx_;

    QColor backgroundColor_;
    QColor gridColor_;
    QColor gFrameColor_;
    QColor gHiddenColor_;
    QColor gInactiveColor_;
    QColor gActiveColor_;
    QColor gSelectedColor_;
    QColor frameColor_;

    LineShaderData selectedShader_;
    std::vector<DecalXY> selectedItems_;
    bool selectedItemsChanged_;

    LineShaderData highlightShader_[8];
    std::vector<DecalXY> highlightItems_[8];
    bool highlightItemsChanged_[8];
    QColor highlightColors[8];
};

NEXTPNR_NAMESPACE_END

#endif
