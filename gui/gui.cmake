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

set(GENERATED_MOC_FILES
    ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_mainwindow.cc
    ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_fpgaviewwidget.cc
    ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_pythontab.cc
    ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_infotab.cc
    ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_designwidget.cc
)

set(UI_SOURCES
    gui/mainwindow.ui
)
qt5_wrap_ui_custom(GENERATED_UI_HEADERS ${UI_SOURCES})
qt5_add_resources_custom(GUI_RESOURCE_FILES gui/nextpnr.qrc)

aux_source_directory(gui/ GUI_ALL_SOURCE_FILES)
set(GUI_SOURCE_FILES ${GUI_ALL_SOURCE_FILES} ${GENERATED_MOC_FILES} ${GENERATED_UI_HEADERS} ${GUI_RESOURCE_FILES})
set(GUI_LIBRARY_FILES Qt5::Widgets Qt5::OpenGL ${OPENGL_LIBRARIES} QtPropertyBrowser)


add_library(QtPropertyBrowser STATIC "")
qt5_generate_moc(3rdparty/QtPropertyBrowser/src/qtpropertybrowser.h ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_qtpropertybrowser.hpp)
qt5_generate_moc(3rdparty/QtPropertyBrowser/src/qtgroupboxpropertybrowser.h ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_qtgroupboxpropertybrowser.hpp)
qt5_generate_moc(3rdparty/QtPropertyBrowser/src/qtvariantproperty.h ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_qtvariantproperty.hpp)
qt5_generate_moc(3rdparty/QtPropertyBrowser/src/qtpropertymanager.h ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_qtpropertymanager.hpp)
qt5_generate_moc(3rdparty/QtPropertyBrowser/src/qtbuttonpropertybrowser.h ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_qtbuttonpropertybrowser.hpp)
qt5_generate_moc(3rdparty/QtPropertyBrowser/src/qteditorfactory.h ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_qteditorfactory.hpp)
qt5_generate_moc(3rdparty/QtPropertyBrowser/src/qttreepropertybrowser.h ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_qttreepropertybrowser.hpp)
qt5_generate_moc(3rdparty/QtPropertyBrowser/src/qtpropertybrowserutils_p.h ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_qtpropertybrowserutils_p.cpp)

qt5_generate_moc(3rdparty/QtPropertyBrowser/src/qtpropertymanager.cpp ${CMAKE_CURRENT_BINARY_DIR}/generated/qtpropertymanager.moc)
qt5_generate_moc(3rdparty/QtPropertyBrowser/src/qttreepropertybrowser.cpp ${CMAKE_CURRENT_BINARY_DIR}/generated/qttreepropertybrowser.moc)
qt5_generate_moc(3rdparty/QtPropertyBrowser/src/qteditorfactory.cpp ${CMAKE_CURRENT_BINARY_DIR}/generated/qteditorfactory.moc)

set(QTPB_GENERATED_MOC_FILES
    ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_qtpropertybrowser.hpp
    ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_qtgroupboxpropertybrowser.hpp
    ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_qtvariantproperty.hpp
    ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_qtpropertymanager.hpp
    ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_qtbuttonpropertybrowser.hpp
    ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_qteditorfactory.hpp
    ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_qttreepropertybrowser.hpp
    ${CMAKE_CURRENT_BINARY_DIR}/generated/qtpropertymanager.moc
    ${CMAKE_CURRENT_BINARY_DIR}/generated/qttreepropertybrowser.moc
    ${CMAKE_CURRENT_BINARY_DIR}/generated/qteditorfactory.moc
    ${CMAKE_CURRENT_BINARY_DIR}/generated/moc_qtpropertybrowserutils_p.cpp
)

qt5_add_resources_custom(QTPB_RESOURCE_FILES 3rdparty/QtPropertyBrowser/src/qtpropertybrowser.qrc)
aux_source_directory(3rdparty/QtPropertyBrowser/src/ QTPROPBROWSER_SRC_ALL)
target_sources(QtPropertyBrowser PRIVATE ${QTPROPBROWSER_SRC_ALL} ${QTPB_GENERATED_MOC_FILES} ${QTPB_RESOURCE_FILES})
target_include_directories(QtPropertyBrowser PRIVATE 3rdparty/QtPropertyBrowser/src generated)
target_link_libraries(QtPropertyBrowser PRIVATE Qt5::Core Qt5::Widgets)

