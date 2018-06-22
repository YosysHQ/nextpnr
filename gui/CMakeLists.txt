set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

aux_source_directory(. GUI_SOURCE_FILES)
aux_source_directory(${family}/ GUI_SOURCE_FILES)
set(_RESOURCES base.qrc ${family}/nextpnr.qrc)
  
qt5_add_resources(GUI_RESOURCE_FILES ${_RESOURCES})

set(GUI_LIBRARY_FILES_${ufamily} Qt5::Widgets Qt5::OpenGL ${OPENGL_LIBRARIES} QtPropertyBrowser PARENT_SCOPE)

add_library(gui_${family} STATIC ${GUI_SOURCE_FILES} ${GUI_RESOURCE_FILES})

target_include_directories(gui_${family} PRIVATE ../${family} ${family} ../3rdparty/QtPropertyBrowser/src)
target_compile_definitions(gui_${family} PRIVATE NEXTPNR_NAMESPACE=nextpnr_${family} ARCH_${ufamily} ARCHNAME=${family} QT_NO_KEYWORDS)
target_link_libraries(gui_${family} Qt5::Widgets)
