#ifndef PYTHON_CONSOLE_UTILS_H
#define PYTHON_CONSOLE_UTILS_H
#include <string>
#include <vector>

/**
InputIterator has value type of std::string.
*/
template < class InputIterator >
std::string LongestCommonPrefix( InputIterator begin, InputIterator end )
{
    if ( begin == end )
        return "";

    const std::string& str0 = *begin;
    if ( ! str0.size() )
        return "";

    int endIndex = str0.size() - 1;
    InputIterator it = begin; ++it;
    for (; it != end; ++it)
    {
        const std::string& str = *it;
        for (int j = 0; j <= endIndex; ++j)
        {
            if (j >= (int)str.size() || str[j] != str0[j])
                endIndex = j - 1;
        }
    }
    return (endIndex > 0)? str0.substr(0, endIndex + 1) : "";
}

#endif // PYTHON_CONSOLE_UTILS_H
