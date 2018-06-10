#include "fpgaviewwidget.h"
#include <QCoreApplication>
#include <QMouseEvent>
#include <math.h>
#include "mainwindow.h"

FPGAViewWidget::FPGAViewWidget(QWidget *parent)
        : QOpenGLWidget(parent), m_xMove(0), m_yMove(0), m_zDistance(1.0)
{
    design = static_cast<MainWindow *>(parent)->getDesign();
}

FPGAViewWidget::~FPGAViewWidget() {}

QSize FPGAViewWidget::minimumSizeHint() const { return QSize(640, 480); }

QSize FPGAViewWidget::sizeHint() const { return QSize(640, 480); }

void FPGAViewWidget::setXTranslation(float t_x)
{
    if (t_x != m_xMove) {
        m_xMove = t_x;
        update();
    }
}

void FPGAViewWidget::setYTranslation(float t_y)
{
    if (t_y != m_yMove) {
        m_yMove = t_y;
        update();
    }
}

void FPGAViewWidget::setZoom(float t_z)
{
    if (t_z != m_zDistance) {
        m_zDistance -= t_z;
        if (m_zDistance < 0.1f)
            m_zDistance = 0.1f;
        if (m_zDistance > 10.0f)
            m_zDistance = 10.0f;

        update();
    }
}

void FPGAViewWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(1.0, 1.0, 1.0, 0.0);
}

void FPGAViewWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();

    glTranslatef(m_xMove, m_yMove, -10.0);
    glScalef(m_zDistance, m_zDistance, 0.0f);

    // Example grid
    glColor3f(0.8, 0.8, 0.8);
    glBegin(GL_LINES);
    for (float i = -100; i <= 100; i += 0.1) {
        glVertex3f((float)i, -100.0f, 0.0f);
        glVertex3f((float)i, 100.0f, 0.0f);
        glVertex3f(-100.0f, (float)i, 0.0f);
        glVertex3f(100.0f, (float)i, 0.0f);
    }
    glColor3f(0.5, 0.5, 0.5);
    for (int i = -100; i <= 100; i += 1) {
        glVertex3f((float)i, -100.0f, 0.0f);
        glVertex3f((float)i, 100.0f, 0.0f);
        glVertex3f(-100.0f, (float)i, 0.0f);
        glVertex3f(100.0f, (float)i, 0.0f);
    }
    glEnd();

    // Example triangle
    glBegin(GL_TRIANGLES);
    glColor3f(1.0, 0.0, 0.0);
    glVertex3f(-0.5, -0.5, 0);
    glColor3f(0.0, 1.0, 0.0);
    glVertex3f(0.5, -0.5, 0);
    glColor3f(0.0, 0.0, 1.0);
    glVertex3f(0, 0.5, 0);
    glEnd();
}

void FPGAViewWidget::resizeGL(int width, int height)
{
    m_windowWidth = width;
    m_windowHeight = height;
    glViewport(0, 0, m_windowWidth, m_windowHeight);

    float aspect = width * 1.0 / height;

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0 * aspect, +1.0 * aspect, -1.0, +1.0, 1.0, 15.0);
    glMatrixMode(GL_MODELVIEW);
}

void FPGAViewWidget::mousePressEvent(QMouseEvent *event)
{
    m_lastPos = event->pos();
}

void FPGAViewWidget::mouseMoveEvent(QMouseEvent *event)
{
    int dx = event->x() - m_lastPos.x();
    int dy = event->y() - m_lastPos.y();
    float dx_scale = dx * (1 / (float)640);
    float dy_scale = -dy * (1 / (float)480);

    if (event->buttons() & Qt::LeftButton) {
        float xpos = m_xMove + dx_scale;
        float ypos = m_yMove + dy_scale;
        if (m_xMove / m_zDistance <= 100.0 && m_xMove / m_zDistance >= -100.0)
            setXTranslation(xpos);
        if (m_yMove / m_zDistance <= 100.0 && m_yMove / m_zDistance >= -100.0)
            setYTranslation(ypos);
    }
    m_lastPos = event->pos();
}

void FPGAViewWidget::wheelEvent(QWheelEvent *event)
{
    QPoint degree = event->angleDelta() / 8;

    if (!degree.isNull()) {
        QPoint step = degree / 15;
        setZoom(step.y() * -0.1f);
    }
}
