/**
python-console
Copyright (C) 2014  Alex Tsui

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#ifndef PYINTERPRETER_H
#define PYINTERPRETER_H
#include <list>
#include <string>

std::string pyinterpreter_execute(const std::string &command, int *errorCode);
const std::list<std::string> &pyinterpreter_suggest(const std::string &hint);
void pyinterpreter_preinit();
void pyinterpreter_initialize();
void pyinterpreter_finalize();
void pyinterpreter_aquire();
void pyinterpreter_release();
std::string pyinterpreter_execute_file(const char *python_file, int *errorCode);
#endif // PYINTERPRETER_H
