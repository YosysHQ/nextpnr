#include "QtImGui.h"
#include "ImGuiRenderer.h"
#include <QWindow>
#ifdef QT_WIDGETS_LIB
#include <QWidget>
#endif

namespace QtImGui {

#ifdef QT_WIDGETS_LIB

namespace {

class QWidgetWindowWrapper : public WindowWrapper {
public:
    QWidgetWindowWrapper(QWidget *w) : w(w) {}
    void installEventFilter(QObject *object) override {
        return w->installEventFilter(object);
    }
    QSize size() const override {
        return w->size();
    }
    qreal devicePixelRatio() const override {
        return w->devicePixelRatio();
    }
    bool isActive() const override {
        return w->isActiveWindow();
    }
    QPoint mapFromGlobal(const QPoint &p) const override {
        return w->mapFromGlobal(p);
    }
private:
    QWidget *w;
};

}

void initialize(QWidget *window) {
    ImGuiRenderer::instance()->initialize(new QWidgetWindowWrapper(window));
}

#endif

namespace {

class QWindowWindowWrapper : public WindowWrapper {
public:
    QWindowWindowWrapper(QWindow *w) : w(w) {}
    void installEventFilter(QObject *object) override {
        return w->installEventFilter(object);
    }
    QSize size() const override {
        return w->size();
    }
    qreal devicePixelRatio() const override {
        return w->devicePixelRatio();
    }
    bool isActive() const override {
        return w->isActive();
    }
    QPoint mapFromGlobal(const QPoint &p) const override {
        return w->mapFromGlobal(p);
    }
private:
    QWindow *w;
};

}

void initialize(QWindow *window) {
    ImGuiRenderer::instance()->initialize(new QWindowWindowWrapper(window));
}

void newFrame() {
    ImGuiRenderer::instance()->newFrame();
}

}
