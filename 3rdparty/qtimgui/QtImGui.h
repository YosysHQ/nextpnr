#pragma once

class QWidget;
class QWindow;

namespace QtImGui {

#ifdef QT_WIDGETS_LIB
void initialize(QWidget *window);
#endif

void initialize(QWindow *window);
void newFrame();

}
