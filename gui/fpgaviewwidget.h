#ifndef MAPGLWIDGET_H
#define MAPGLWIDGET_H

#include <QMainWindow>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QPainter>
#include "design.h"

class FPGAViewWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

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
    void drawElement(const GraphicElement &el);
    QMainWindow *getMainWindow();

  private:
    int m_windowWidth;
    int m_windowHeight;
    float m_xMove;
    float m_yMove;
    float m_zDistance;
    QPoint m_lastPos;
    Design *design;
};
#endif
