cmake_minimum_required( VERSION 2.8 )
project( PythonInterpreter )

set(CMAKE_CXX_STANDARD 11)
find_package(Qt5 COMPONENTS Core Widgets REQUIRED)

find_package( PythonLibs REQUIRED )
include_directories( ${PYTHON_INCLUDE_DIRS} )

add_executable( test_python_interpreter test_python_interpreter.cpp Interpreter.cpp )
target_link_libraries( test_python_interpreter ${PYTHON_LIBRARIES} )

qt5_wrap_cpp( Console_MOC Console.h )
add_executable( test_console test_console.cpp
    Console.cpp ${Console_MOC}
    ColumnFormatter.cpp
    Interpreter.cpp
    ParseHelper.cpp
    ParseHelper.BlockParseState.cpp
    ParseHelper.BracketParseState.cpp
    ParseHelper.ContinuationParseState.cpp
    ParseMessage.cpp
)
target_compile_definitions( test_console PRIVATE QT_NO_KEYWORDS)
target_link_libraries( test_console Qt5::Widgets ${PYTHON_LIBRARIES} )

add_executable( test_parse_helper test_parse_helper.cpp 
    ParseHelper.cpp
    ParseHelper.BlockParseState.cpp
    ParseHelper.BracketParseState.cpp
    ParseHelper.ContinuationParseState.cpp
    ParseListener.cpp
    ParseMessage.cpp
)

add_executable( test_cli test_cli.cpp 
    ParseHelper.cpp
    ParseHelper.BlockParseState.cpp
    ParseHelper.BracketParseState.cpp
    ParseHelper.ContinuationParseState.cpp
    ParseListener.cpp
    ParseMessage.cpp
    Interpreter.cpp
)
target_link_libraries( test_cli
    ${PYTHON_LIBRARIES}
)
