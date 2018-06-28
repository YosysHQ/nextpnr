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
#ifndef COLUMN_FORMATTER_H
#define COLUMN_FORMATTER_H
#include <list>
#include <string>
#include <vector>

/**
Format a list of items into as many columns as width permits.
*/
class ColumnFormatter
{
protected:
    std::vector<std::string> items;
    std::vector< std::list<std::string> > columns;
    std::list< std::string > m_formattedOutput;

public:
    /**
    Load items from file, one item per line.
    */
    bool load(const std::string& fn);

    template <class InputIterator>
    void setItems(InputIterator begin, InputIterator end)
    {
        items.clear();
        for (InputIterator it = begin; it != end; ++it)
        {
            items.push_back(*it);
        }
    }

    /**
    Determine the number of columns that the items can be into, given a
    width limit.
    */
    int solve(int width);

    /**
    Divide items into numColumns.
    */
    std::vector<size_t> divideItems(int numColumns);

    /**
    Generate formatted output, the items formatted into as many columns as can
    be fit in width.
    */
    void format(int width);

    /**
    Get output.
    */
    const std::list<std::string>& formattedOutput() const;
};

#endif // COLUMN_FORMATTER_H
