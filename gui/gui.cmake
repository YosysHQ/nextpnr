# Find the Qt5 libraries
find_package(Qt5 COMPONENTS Core Widgets OpenGL REQUIRED)
find_package(OpenGL REQUIRED)

include( gui/Qt5Customizations.cmake )

# Find includes in corresponding build directories
include_directories(${CMAKE_CURRENT_BINARY_DIR}/generated)

qt5_generate_moc(gui/mainwindow.h ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_mainwindow.cc)
qt5_generate_moc(gui/fpgaviewwidget.h ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_fpgaviewwidget.cc)
qt5_generate_moc(gui/pythontab.h ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_pythontab.cc)
qt5_generate_moc(gui/infotab.h ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_infotab.cc)
qt5_generate_moc(gui/designwidget.h ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_designwidget.cc)
qt5_generate_moc(gui/line_editor.h ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_line_editor.cc)

set(GENERATED_MOC_FILES
    ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_mainwindow.cc
    ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_fpgaviewwidget.cc
    ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_pythontab.cc
    ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_infotab.cc
    ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_designwidget.cc
    ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_line_editor.cc
)

qt5_add_resources_custom(GUI_RESOURCE_FILES gui/nextpnr.qrc)

aux_source_directory(gui/ GUI_ALL_SOURCE_FILES)
set(GUI_SOURCE_FILES ${GUI_ALL_SOURCE_FILES} ${GENERATED_MOC_FILES} ${GENERATED_UI_HEADERS} ${GUI_RESOURCE_FILES})
set(GUI_LIBRARY_FILES Qt5::Widgets Qt5::OpenGL ${OPENGL_LIBRARIES} QtPropertyBrowser)
