set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

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

if (Qt6_FOUND)
    qt6_add_resources(GUI_QT_RESOURCES
        base.qrc
        ${family}/nextpnr.qrc
    )
else()
    qt5_add_resources(GUI_QT_RESOURCES
        base.qrc
        ${family}/nextpnr.qrc
    )
endif()

add_library(nextpnr-${target}-gui OBJECT
    ${GUI_SOURCES}
    ${GUI_QT_RESOURCES}
)

target_compile_definitions(nextpnr-${target}-gui PUBLIC
    QT_NO_KEYWORDS
)

target_include_directories(nextpnr-${target}-gui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/${family}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(nextpnr-${target}-gui PRIVATE
    ${CMAKE_SOURCE_DIR}/frontend
    ${CMAKE_SOURCE_DIR}/json
    ${CMAKE_SOURCE_DIR}/3rdparty/QtPropertyBrowser/src
    ${CMAKE_SOURCE_DIR}/3rdparty/imgui
    ${CMAKE_SOURCE_DIR}/3rdparty/qtimgui
)

if (Qt6_FOUND)
    target_link_libraries(nextpnr-${target}-gui PUBLIC Qt6::Widgets)
    target_link_libraries(nextpnr-${target}-gui PRIVATE Qt6::OpenGL Qt6::OpenGLWidgets)
else()
    target_link_libraries(nextpnr-${target}-gui PUBLIC Qt5::Widgets)
    target_link_libraries(nextpnr-${target}-gui PRIVATE Qt5::OpenGL)
endif()

target_link_libraries(nextpnr-${target}-gui PRIVATE
    nextpnr-${target}-defs
    nextpnr_version
    QtPropertyBrowser
    pybind11::headers
)

# Currently always the case when the GUI is built.
if (BUILD_PYTHON)
    target_sources(nextpnr-${target}-gui PRIVATE
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

    target_include_directories(nextpnr-${target}-gui PRIVATE
        ../3rdparty/python-console
        ../3rdparty/python-console/modified
    )
endif()
