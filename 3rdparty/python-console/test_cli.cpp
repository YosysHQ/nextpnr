#include <iostream>
#include <string>
#include "ParseHelper.h"
#include "ParseListener.h"
#include "Interpreter.h"

const std::string STD_PROMPT = ">>> ";
const std::string MULTILINE_PROMPT = "... ";

struct InterpreterRelay : public ParseListener
{
    Interpreter* m_interpreter;

    InterpreterRelay( ):
        m_interpreter( new Interpreter )
    { }

    virtual void parseEvent( const ParseMessage& msg )
    {
        if ( msg.errorCode )
        {
            std::cout << "(" << msg.errorCode << ") " << msg.message << "\n";
            return;
        }
        else
        {
            int err;
            std::string res = m_interpreter->interpret( msg.message, &err );
            std::cout << "(" << msg.errorCode << ") " << res << "\n";
        }
    }
};

int main( int argc, char *argv[] )
{
    Interpreter::Initialize( );
    const std::string* prompt = &STD_PROMPT;
    ParseHelper helper;
    ParseListener* listener = new InterpreterRelay;
    helper.subscribe( listener );

    std::string str;
    std::cout << *prompt;
    std::getline( std::cin, str );
    while ( str != "quit" )
    {
        std::cout << str << "\n";
        helper.process( str );
        if ( helper.buffered( ) )
        {
            prompt = &MULTILINE_PROMPT;
        }
        else
        {
            prompt = &STD_PROMPT;
        }
        std::cout << *prompt;
        std::getline( std::cin, str );
    }

    Interpreter::Finalize( );
    return 0;
}
