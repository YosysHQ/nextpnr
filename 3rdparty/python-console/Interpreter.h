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

#ifndef INTERPRETER_H
#define INTERPRETER_H
#include <string>
#include <list>
#include <Python.h>
#if defined(__APPLE__) && defined(__MACH__)
/*
 * The following undefs for C standard library macros prevent
 * build errors of the following type on mac ox 10.7.4 and XCode 4.3.3
 *
/usr/include/c++/4.2.1/bits/localefwd.h:57:21: error: too many arguments provided to function-like macro invocation
    isspace(_CharT, const locale&);
                    ^
/usr/include/c++/4.2.1/bits/localefwd.h:56:5: error: 'inline' can only appear on functions
    inline bool
    ^
/usr/include/c++/4.2.1/bits/localefwd.h:57:5: error: variable 'isspace' declared as a template
    isspace(_CharT, const locale&);
    ^
*/
#undef isspace
#undef isupper
#undef islower
#undef isalpha
#undef isalnum
#undef toupper
#undef tolower
#endif

/**
Wraps a Python interpreter, which you can pass commands as strings to interpret
and get strings of output/error in return.
*/
class Interpreter
{
protected:
    static PyThreadState* MainThreadState;

    PyThreadState* m_threadState;
    PyObject* glb;
    PyObject* loc;

    std::list< std::string > m_suggestions;

public:
    /**
    Instantiate a Python interpreter.
    */
    Interpreter( );
    virtual ~Interpreter( );
    
    void test( );
        std::string interpret( const std::string& command, int* errorCode );
    const std::list<std::string>& suggest( const std::string& hint );

    /**
    Call this before constructing and using Interpreter.
    */
    static void Initialize( );

    /**
    Call this when done using Interpreter.
    */
    static void Finalize( );

protected:
    static PyObject* PyInit_redirector(void);
    static PyObject* RedirectorInit(PyObject *, PyObject *);
    static PyObject* RedirectorWrite(PyObject *, PyObject *args);
    static std::string& GetResultString( PyThreadState* threadState );
    static PyMethodDef ModuleMethods[];
    static PyMethodDef RedirectorMethods[];
};
#endif // INTERPRETER_H
