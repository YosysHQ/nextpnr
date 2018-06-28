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
#include "ColumnFormatter.h"
#include <iomanip>
#include <iostream>
#include <list>
#include <string>
#include <sstream>
#include <fstream>
#include <vector>
#include <cstdlib>

bool ColumnFormatter::load(const std::string& fn)
{
    items.clear();
    std::ifstream ifs(fn.c_str());
    if (!ifs.is_open())
        return false;
    std::string str;
    while (!ifs.eof())
    {
        std::getline( ifs, str );
        if (!ifs.eof())
            items.push_back(str);
    }
    return true;
}

int ColumnFormatter::solve(int width)
{
    bool fits = true;
    int i = 1;
    while (fits)
    {
        ++i;
        std::vector<size_t> widths = divideItems(i);
        size_t columnWidth = width / i;
        for (size_t j = 0; j < widths.size(); ++j)
        {
            fits &= (widths[j] < columnWidth);
        }
        if (!fits)
        {
            --i;
        }
    }
    return i;
}

std::vector<size_t> ColumnFormatter::divideItems(int numColumns)
{
    columns.clear();
    for (int i = 0; i < numColumns; ++i)
    columns.push_back(std::list<std::string>());
    for (size_t i = 0; i < items.size(); ++i)
    {
        columns[i % numColumns].push_back(items[i]);
    }

    // count the fattest item in each column
    std::vector<size_t> res(numColumns);
    for (int i = 0; i < numColumns; ++i)
    {
        for (std::list<std::string>::const_iterator it =
            columns[i].begin(); it != columns[i].end(); ++it)
        {
            if (res[i] < it->size())
                res[i] = it->size();
        }
    }

    return res;
}

void ColumnFormatter::format(int width)
{
    m_formattedOutput.clear();
    int cols = solve(width);
    std::vector<int> colWidths(cols, width / cols);
    int rem = width % cols;
    for (int i = 0; i < rem; ++i)
    {
        colWidths[i]++;
    }
    divideItems(cols);
    std::vector< std::list<std::string>::const_iterator > its;
    std::vector< std::list<std::string>::const_iterator > it_ends;
    for (size_t i = 0; i < columns.size(); ++i)
    {
        its.push_back(columns[i].begin());
        it_ends.push_back(columns[i].end());
    }
    bool done = false;
    while (!done)
    {
        std::stringstream row_ss;
        for (size_t i = 0; i < columns.size(); ++i)
        {
            std::stringstream item_ss;
            std::string item;
            if (its[i] != it_ends[i])
            {
                item = *its[i];
                ++its[i];
            }
            item_ss << std::left << std::setw(colWidths[i]) << item;
            row_ss << item_ss.str();
        }
        m_formattedOutput.push_back(row_ss.str());

        done = true;
        for (size_t i = 0; i < columns.size(); ++i)
        {
            done &= (its[i] == it_ends[i]);
        }
    }
}

const std::list<std::string>& ColumnFormatter::formattedOutput() const
{
    return m_formattedOutput;
}
