SET(example_name simple)

SET(KIT_SRCS
  main.cpp
  )
  
ADD_EXECUTABLE(${example_name} ${KIT_SRCS})
TARGET_LINK_LIBRARIES(${example_name} ${PROJECT_NAME})
