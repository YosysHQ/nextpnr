# Find the Qt5 libraries
find_package(Qt5 COMPONENTS Core Widgets OpenGL REQUIRED)
find_package(OpenGL REQUIRED)

ADD_DEFINITIONS(-DQT_NO_KEYWORDS)

include( gui/Qt5Customizations.cmake )

# Find includes in corresponding build directories
include_directories(${CMAKE_CURRENT_BINARY_DIR}/generated)

qt5_generate_moc(gui/mainwindow.h ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_mainwindow.cc)
qt5_generate_moc(gui/fpgaviewwidget.h ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_fpgaviewwidget.cc)

set(GENERATED_MOC_FILES
    ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_mainwindow.cc
    ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_fpgaviewwidget.cc
)

set(UI_SOURCES
    gui/mainwindow.ui
)
qt5_wrap_ui_custom(GENERATED_UI_HEADERS ${UI_SOURCES})
qt5_add_resources_custom(GUI_RESOURCE_FILES gui/nextpnr.qrc)

set(GUI_SOURCE_FILES gui/mainwindow.cc gui/fpgaviewwidget.cc gui/emb.cc ${GENERATED_MOC_FILES} ${GENERATED_UI_HEADERS} ${GUI_RESOURCE_FILES})
set(GUI_LIBRARY_FILES Qt5::Widgets Qt5::OpenGL ${OPENGL_LIBRARIES})