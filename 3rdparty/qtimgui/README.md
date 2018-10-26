# QtImGui

Qt (QOpenGLWidget / QOpenGLWindow) backend for [ImGui](https://github.com/ocornut/imgui)

It enables ImGui to run in QOpenGLWidget / QOpenGLWindow.

[![https://gyazo.com/eb68699c96b9147cca3d5ea9fadfc263](https://i.gyazo.com/eb68699c96b9147cca3d5ea9fadfc263.gif)](https://gyazo.com/eb68699c96b9147cca3d5ea9fadfc263)

## How to use

* Add QtImGui sources and headers to your project
    * If you are using git submodule, run `git submodule update --init --recursive` to ensure that the inner submodule is initialized as well.
* Add `include(path/to/qtimgui.pri)` to youre `.pro` file
* Subclass `QOpenGLWindow` or `QOpenGLWidget` and:

```cpp
class DemoWindow : public QOpenGLWindow
{
protected:
    void initializeGL() override
    {
        QtImGui::initialize(this);
    }
    void paintGL() override
    {
        // you can do custom GL rendering as well in paintGL

        QtImGui::newFrame();

        ImGui::Text("Hello");
        // more widgets...

        ImGui::Render();
    }
};
```

See [QOpenGLWidget example](demo-widget/demo-widget.cpp) and [QOpenGLWindow example](/demo-window/demo-window.cpp) for details.
