set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

add_compile_definitions(QT_NO_KEYWORDS)

set(GUI_SOURCES
    application.cc
    application.h
    basewindow.cc
    basewindow.h
    designwidget.cc
    designwidget.h
    fpgaviewwidget.cc
    fpgaviewwidget.h
    line_editor.cc
    line_editor.h
    lineshader.cc
    lineshader.h
    pyconsole.cc
    pyconsole.h
    pythontab.cc
    pythontab.h
    quadtree.h
    treemodel.cc
    treemodel.h
    worker.cc
    worker.h
    ${family}/mainwindow.cc
    ${family}/mainwindow.h
)

qt5_add_resources(GUI_QT_RESOURCES
    base.qrc
    ${family}/nextpnr.qrc
)

add_library(gui_${family} STATIC
    ${GUI_SOURCES}
    ${GUI_QT_RESOURCES}
)

target_include_directories(gui_${family} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/${family}
    ${CMAKE_SOURCE_DIR}/${family}
    ${CMAKE_SOURCE_DIR}/3rdparty/QtPropertyBrowser/src
    ${CMAKE_SOURCE_DIR}/3rdparty/imgui
    ${CMAKE_SOURCE_DIR}/3rdparty/qtimgui
    ${CMAKE_BINARY_DIR}/generated
)

target_compile_definitions(gui_${family} PRIVATE
    NEXTPNR_NAMESPACE=nextpnr_${family}
    ARCH_${ufamily}
    ARCHNAME=${family}
)

target_link_libraries(gui_${family} PUBLIC
    Qt5::Widgets
)
target_link_libraries(gui_${family} PRIVATE
    nextpnr_kernel
    Qt5::OpenGL
    QtPropertyBrowser
    ${EXTRA_LIB_DEPS}
)

# Currently always the case when the GUI is built.
if (BUILD_PYTHON)
    target_sources(gui_${family} PRIVATE
        ../3rdparty/python-console/ColumnFormatter.cpp
        ../3rdparty/python-console/ParseHelper.cpp
        ../3rdparty/python-console/ParseHelper.BlockParseState.cpp
        ../3rdparty/python-console/ParseHelper.BracketParseState.cpp
        ../3rdparty/python-console/ParseHelper.ContinuationParseState.cpp
        ../3rdparty/python-console/ParseMessage.cpp
        ../3rdparty/python-console/modified/pyredirector.cc
        ../3rdparty/python-console/modified/pyinterpreter.cc
        ../3rdparty/imgui/imgui_widgets.cpp
        ../3rdparty/imgui/imgui_draw.cpp
        ../3rdparty/imgui/imgui.cpp
        ../3rdparty/imgui/imgui_demo.cpp
        ../3rdparty/qtimgui/ImGuiRenderer.cpp
        ../3rdparty/qtimgui/QtImGui.cpp
    )

    target_include_directories(gui_${family} PRIVATE
        ../3rdparty/python-console
        ../3rdparty/python-console/modified
    )
endif()
