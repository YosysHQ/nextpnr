#include <iostream>
#include "Interpreter.h"

int main( int argc, char *argv[] )
{
    std::string commands[] = {
        "from time import time,ctime\n",
        "print('Today is',ctime(time()))\n"
    };
    Interpreter::Initialize( );
    Interpreter* interpreter = new Interpreter;
    for ( int i = 0; i < 2; ++i )
    {
        int err;
        std::string res = interpreter->interpret( commands[i], &err );
        std::cout << res;
    }
    delete interpreter;

    Interpreter::Finalize( );
    return 0;
}
