/**
Copyright (c) 2014 Alex Tsui

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/
#include "ParseHelper.h"
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <cstdlib>
#include "ParseListener.h"

ParseHelper::Indent::
Indent( )
{ }

ParseHelper::Indent::
Indent( const std::string& indent ):
    Token( indent )
{ }

ParseHelper::ParseState::
ParseState( ParseHelper& parent_ ): parent( parent_ )
{ }

ParseHelper::ParseState::
~ParseState( )
{ }

bool ParseHelper::PeekIndent( const std::string& str, Indent* indent )
{
    if ( !str.size() || ! isspace(str[0]) )
        return false;

    int nonwhitespaceIndex = -1;
    for (size_t i = 0; i < str.size(); ++i)
    {
        if (!isspace(str[i]))
        {
            nonwhitespaceIndex = i;
            break;
        }
    }
    if (nonwhitespaceIndex == -1)
    {
        return false;
    }
    std::string indentToken = str.substr(0, nonwhitespaceIndex);
    indent->Token = indentToken;
    return true;
}

ParseHelper::ParseHelper( )
{ }

void ParseHelper::process( const std::string& str )
{
    std::string top;
    commandBuffer.push_back(str);
    //std::string top = commandBuffer.back();
    //commandBuffer.pop_back();
    std::shared_ptr<ParseState> blockStatePtr;
    while (stateStack.size())
    {
        top = commandBuffer.back();
        commandBuffer.pop_back();
        blockStatePtr = stateStack.back();
        if (blockStatePtr->process(top))
            return;
    }

    if ( ! commandBuffer.size() )
        return;

    // standard state
    top = commandBuffer.back();
    if ( !top.size() )
    {
        reset( );
        broadcast( std::string() );
        return;
    }

    { // check for unexpected indent
        Indent ind;
        bool isIndented = PeekIndent( top, &ind );
        if ( isIndented &&
            ! isInContinuation( ) )
        {
            reset( );
            ParseMessage msg( 1, "IndentationError: unexpected indent");
            broadcast( msg );
            return;
        }
    }

    // enter indented block state
    if ( top[top.size()-1] == ':' )
    {
        std::shared_ptr<ParseState> parseState(
            new BlockParseState( *this ) );
        stateStack.push_back( parseState );
        return;
    }

    if ( top[top.size()-1] == '\\' )
    {
        std::shared_ptr<ParseState> parseState(
            new ContinuationParseState( *this ) );
        stateStack.push_back( parseState );
        return;
    }

    if (BracketParseState::HasOpenBrackets( top ))
    {
        // FIXME: Every parse state should have its own local buffer
        commandBuffer.pop_back( );
        std::shared_ptr<ParseState> parseState(
            new BracketParseState( *this, top ) );
        stateStack.push_back( parseState );
        return;
    }

    // handle single-line statement
    flush( );
}

bool ParseHelper::buffered( ) const
{
    return commandBuffer.size( ) || stateStack.size( );
}

void ParseHelper::flush( )
{
    std::stringstream ss;
    for (size_t i = 0; i < commandBuffer.size(); ++i )
    {
        ss << commandBuffer[i] << "\n";
    }
    commandBuffer.clear();

    broadcast( ss.str() );
    // TODO: feed string to interpreter
}

void ParseHelper::reset( )
{
//    inContinuation = false;
    stateStack.clear( );
    commandBuffer.clear( );
}

bool ParseHelper::isInContinuation( ) const
{
    return (stateStack.size()
        && (std::dynamic_pointer_cast<ContinuationParseState>(
            stateStack.back())));
}

void ParseHelper::subscribe( ParseListener* listener )
{
    listeners.push_back( listener );
}

void ParseHelper::unsubscribeAll( )
{
    listeners.clear( );
}

void ParseHelper::broadcast( const ParseMessage& msg )
{
    // broadcast signal
    for (size_t i = 0; i < listeners.size(); ++i)
    {
        if (listeners[i])
        {
            listeners[i]->parseEvent(msg);
        }
    }
}
