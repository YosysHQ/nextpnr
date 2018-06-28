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

ParseHelper::BlockParseState::
BlockParseState( ParseHelper& parent ):
    ParseState( parent )
{ }

ParseHelper::BlockParseState::
BlockParseState( ParseHelper& parent, const std::string& indent_ ):
    ParseState( parent ),
    indent( indent_ )
{ }

bool ParseHelper::BlockParseState::
process(const std::string& str)
{
    bool ok = initializeIndent(str);
    if ( ! ok )
    {
        // finish processing
        return true;
    }

    Indent ind;
    bool isIndented = PeekIndent( str, &ind );
    if ( isIndented )
    {
#ifndef NDEBUG
        std::cout << "current line indent: ";
        print( ind );
#endif
        // check if indent matches
        if ( ind.Token != indent.Token )
        {
            // dedent until we match or empty the stack
            bool found = false;
            while ( !found )
            {
                parent.stateStack.pop_back( );
                if ( !parent.stateStack.size( ) )
                    break;
                boost::shared_ptr<BlockParseState> parseState =
                    boost::dynamic_pointer_cast<BlockParseState>(
                        parent.stateStack.back( ));
                found = ( ind.Token == parseState->indent.Token );
            }

            if ( ! found )
            {
#ifndef NDEBUG
                std::cout << "indent mismatch\n";
#endif
                parent.reset( );
                ParseMessage msg( 1, "IndentationError: unexpected indent");
                parent.broadcast( msg );
                return true;
            }
        }

        // process command

        // enter indented block state
        if ( str[str.size()-1] == ':' )
        {
            parent.commandBuffer.push_back( str );
            //parent.inBlock = (boost::dynamic_pointer_cast<BlockParseState>(
            //    parent.stateStack.back()));

            //expectingIndent = true;
            boost::shared_ptr<ParseState> parseState(
                new BlockParseState( parent ) );
            parent.stateStack.push_back( parseState );
            return true;
        }

        if ( str[str.size()-1] == '\\' )
        {
            parent.commandBuffer.push_back( str );
            boost::shared_ptr<ParseState> parseState(
                new ContinuationParseState( parent ) );
            parent.stateStack.push_back( parseState );
            return true;
        }

        if (BracketParseState::HasOpenBrackets( str ))
        {
            // FIXME: Every parse state should have its own local buffer
            boost::shared_ptr<ParseState> parseState(
                new BracketParseState( parent, str ) );
            parent.stateStack.push_back( parseState );
            return true;
        }

        parent.commandBuffer.push_back( str );
        return true;
    }
    else
    {
        if ( str.size() )
        {
            {
#ifndef NDEBUG
                std::cout << "Expected indented block\n";
#endif
                parent.reset( );
                ParseMessage msg( 1, "IndentationError: expected an indented block" );
                parent.broadcast( msg );
                return false;
            }
        }
#ifndef NDEBUG
        std::cout << "Leaving block\n";
#endif
        parent.stateStack.pop_back();
        parent.flush( );
        parent.reset( );
        return true;
    }
}

bool ParseHelper::BlockParseState::
initializeIndent(const std::string& str)
{
    bool expectingIndent = (indent.Token == "");
    if ( !expectingIndent )
    {
        std::cout << "already initialized indent: ";
        print( indent );
        return true;
    }

    Indent ind;
    bool isIndented = parent.PeekIndent( str, &ind );
    if ( !isIndented )
    {
#ifndef NDEBUG
        std::cout << "Expected indented block\n";
#endif
        parent.reset( );
        ParseMessage msg( 1, "IndentationError: expected an indented block" );
        parent.broadcast( msg );
        return false;
    }
    indent = ind;
    //parent.currentIndent = ind;
    //parent.indentStack.push_back( parent.currentIndent );
    //parent.expectingIndent = false;
#ifndef NDEBUG
    std::cout << "initializing indent: ";
    print( ind );
#endif
    return true;
}
