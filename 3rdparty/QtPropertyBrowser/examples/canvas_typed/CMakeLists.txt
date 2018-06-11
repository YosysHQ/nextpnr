# Tell CMake to run moc when necessary:
set(CMAKE_AUTOMOC ON)

# As moc files are generated in the binary dir, tell CMake
# to always look for includes there:
set(CMAKE_INCLUDE_CURRENT_DIR ON)

SET(example_name canvas_typed)

SET(KIT_SRCS
  main.cpp
  mainwindow.cpp
  mainwindow.h
  qtcanvas.cpp
  qtcanvas.h
  )

ADD_EXECUTABLE(${example_name} ${KIT_SRCS})
TARGET_LINK_LIBRARIES(${example_name} ${PROJECT_NAME})
