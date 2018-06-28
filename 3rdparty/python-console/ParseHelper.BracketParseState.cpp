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
#include <string>
#include <sstream>

const std::string ParseHelper::BracketParseState::OpeningBrackets = "[({";
const std::string ParseHelper::BracketParseState::ClosingBrackets = "])}";

bool ParseHelper::BracketParseState::HasOpenBrackets(const std::string& str)
{
    std::list<char> brackets;
    bool hasOpenBrackets = LoadBrackets(str, &brackets);
    return hasOpenBrackets;
}

bool ParseHelper::BracketParseState::LoadBrackets(const std::string& str,
    std::list<char>* stack)
{
    if ( !stack )
        return false;

    stack->clear();
    for (size_t i = 0; i < str.size(); ++i)
    {
        if (OpeningBrackets.find_first_of(str[i]) != std::string::npos)
        {
            stack->push_back(str[i]);
        }
        else
        {
            size_t t = ClosingBrackets.find_first_of(str[i]);
            if (t != std::string::npos)
            {
                if (t != OpeningBrackets.find_first_of(stack->back()))
                    return false;
                stack->pop_back();
            }
        }
    }
    return stack->size();
}

ParseHelper::BracketParseState::BracketParseState( ParseHelper& parent, const std::string& firstLine ):
    ParseState( parent )
{
    /*bool hasOpenBrackets = */ LoadBrackets( firstLine, &brackets );
    //assert( hasOpenBrackets );
    m_buffer.push_back( firstLine );
}

bool ParseHelper::BracketParseState::process(const std::string& str)
{
    // update brackets stack
    for (size_t i = 0; i < str.size(); ++i)
    {
        if (OpeningBrackets.find_first_of(str[i]) != std::string::npos)
        {
            brackets.push_back(str[i]);
        }
        else
        {
            size_t t = ClosingBrackets.find_first_of(str[i]);
            if (t != std::string::npos)
            {
                // reset state if unmatched brackets seen
                if (t != OpeningBrackets.find_first_of(brackets.back()))
                {
                    parent.reset( );
                    ParseMessage msg(1, "Invalid syntax");
                    parent.broadcast( msg );
                    return true;
                }
                brackets.pop_back();
            }
        }
    }

    // if we've cleared the stack, we've finished accepting input here
    if (!brackets.size())
    {
        // squash buffered lines and place it on parent::commandBuffer
        std::stringstream ss;
        for (std::list<std::string>::const_iterator it = m_buffer.begin();
            it != m_buffer.end(); ++it )
        {
            ss << *it << "\n";
        }
        ss << str;
        parent.commandBuffer.push_back(ss.str());
        parent.stateStack.pop_back( );
        return false;
    }
    else
    {
        // buffer and expect more lines
        m_buffer.push_back( str );
        return true;
    }
}
