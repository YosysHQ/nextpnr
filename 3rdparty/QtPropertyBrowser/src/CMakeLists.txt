# Tell CMake to run moc when necessary:
set(CMAKE_AUTOMOC ON)

# As moc files are generated in the binary dir, tell CMake
# to always look for includes there:
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(_SRCS
  qtbuttonpropertybrowser.cpp
  qteditorfactory.cpp
  qtgroupboxpropertybrowser.cpp
  qtpropertybrowser.cpp
  qtpropertybrowserutils.cpp
  qtpropertymanager.cpp
  qttreepropertybrowser.cpp
  qtvariantproperty.cpp
  )

set(_UI_FORMS
  )

set(_RESOURCES
  qtpropertybrowser.qrc
  )
  
if (Qt6_FOUND)
  QT6_WRAP_UI(_UI_SRCS ${_UI_FORMS})
  QT6_ADD_RESOURCES(_QRC_SRCS ${_RESOURCES})
else()
  QT5_WRAP_UI(_UI_SRCS ${_UI_FORMS})
  QT5_ADD_RESOURCES(_QRC_SRCS ${_RESOURCES})
endif()

set(TARGET_NAME ${PROJECT_NAME})

add_library(${TARGET_NAME} STATIC
  ${_SRCS}
  ${_UI_SRCS}
  ${_QRC_SRCS}
  )

target_compile_options(${TARGET_NAME} PRIVATE -Wno-deprecated-declarations)

if (MSVC)
  target_compile_options(${TARGET_NAME} PRIVATE /wd4457 /wd4718)
endif()

if (Qt6_FOUND)
  target_link_libraries(${TARGET_NAME} Qt6::Widgets)
else()
  target_link_libraries(${TARGET_NAME} Qt5::Widgets)
endif()
