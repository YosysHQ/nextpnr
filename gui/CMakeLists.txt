set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if (BUILD_PYTHON)
  set(PYTHON_CONSOLE_SRC 
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
endif()

aux_source_directory(. GUI_SOURCE_FILES)
aux_source_directory(${family}/ GUI_SOURCE_FILES)

set(_RESOURCES base.qrc ${family}/nextpnr.qrc)
  
qt5_add_resources(GUI_RESOURCE_FILES ${_RESOURCES})

set(GUI_LIBRARY_FILES_${ufamily} Qt5::Widgets Qt5::OpenGL ${OPENGL_LIBRARIES} QtPropertyBrowser PARENT_SCOPE)

add_library(gui_${family} STATIC ${GUI_SOURCE_FILES} ${PYTHON_CONSOLE_SRC} ${GUI_RESOURCE_FILES})
include(${family}/family.cmake)

target_include_directories(gui_${family} PRIVATE ../${family} ${family} ../3rdparty/QtPropertyBrowser/src ../3rdparty/imgui ../3rdparty/qtimgui/)
if (BUILD_PYTHON)
  target_include_directories(gui_${family} PRIVATE ../3rdparty/python-console ../3rdparty/python-console/modified)
endif()

target_compile_definitions(gui_${family} PRIVATE NEXTPNR_NAMESPACE=nextpnr_${family} ARCH_${ufamily} ARCHNAME=${family} QT_NO_KEYWORDS)
target_link_libraries(gui_${family} Qt5::Widgets)
