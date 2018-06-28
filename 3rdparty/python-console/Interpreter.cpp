#include "Interpreter.h"
#include <iostream>
#include <map>
#include <boost/format.hpp>

PyThreadState* Interpreter::MainThreadState = NULL;

Interpreter::Interpreter( )
{
    PyEval_AcquireLock( );
    m_threadState = Py_NewInterpreter( );

    PyObject *module = PyImport_ImportModule("__main__");
    loc = glb = PyModule_GetDict(module);
    SetupRedirector( m_threadState );
    PyRun_SimpleString("import sys\n"
        "import redirector\n"
        "sys.path.insert(0, \".\")\n" // add current path
        "sys.stdout = redirector.redirector()\n"
        "sys.stderr = sys.stdout\n"
        "import rlcompleter\n"
        "sys.completer = rlcompleter.Completer()\n"
    );

    PyEval_ReleaseThread( m_threadState );
}

Interpreter::~Interpreter( )
{
#ifndef NDEBUG
    std::cout << "delete interpreter\n";
#endif
    PyEval_AcquireThread( m_threadState );
    Py_EndInterpreter( m_threadState );
    PyEval_ReleaseLock( );
}

void
Interpreter::test( )
{
    PyEval_AcquireThread( m_threadState );

    PyObject* py_result;
    PyObject* dum;
    std::string command = "print 'Hello world'\n";
    py_result = Py_CompileString(command.c_str(), "<stdin>", Py_single_input);
    if ( py_result == 0 )
    {
        std::cout << "Huh?\n";
        PyEval_ReleaseThread( m_threadState );
        return;
    }
    dum = PyEval_EvalCode ((PyCodeObject *)py_result, glb, loc);
    Py_XDECREF (dum);
    Py_XDECREF (py_result);

    std::cout << GetResultString( m_threadState );
    GetResultString( m_threadState ) = "";

    PyEval_ReleaseThread( m_threadState );
}

std::string
Interpreter::interpret( const std::string& command, int* errorCode )
{
    PyEval_AcquireThread( m_threadState );
    *errorCode = 0;

    PyObject* py_result;
    PyObject* dum;
    std::string res;
#ifndef NDEBUG
    std::cout << "interpreting (" << command << ")\n";
#endif
    py_result = Py_CompileString(command.c_str(), "<stdin>", Py_single_input);
    if ( py_result == 0 )
    {
#ifndef NDEBUG
        std::cout << "Huh?\n";
#endif
        if ( PyErr_Occurred( ) )
        {
            *errorCode = 1;
            PyErr_Print( );
            res = GetResultString( m_threadState );
            GetResultString( m_threadState ) = "";
        }

        PyEval_ReleaseThread( m_threadState );
        return res;
    }
    dum = PyEval_EvalCode ((PyCodeObject *)py_result, glb, loc);
    Py_XDECREF (dum);
    Py_XDECREF (py_result);
    if ( PyErr_Occurred( ) )
    {
        *errorCode = 1;
        PyErr_Print( );
    }

    res = GetResultString( m_threadState );
    GetResultString( m_threadState ) = "";

    PyEval_ReleaseThread( m_threadState );
    return res;
}

const std::list<std::string>& Interpreter::suggest( const std::string& hint )
{
    PyEval_AcquireThread( m_threadState );
    m_suggestions.clear();
    int i = 0;
    std::string command = boost::str(
        boost::format("sys.completer.complete('%1%', %2%)\n")
            % hint
            % i);
#ifndef NDEBUG
    std::cout << command << "\n";
#endif
    std::string res;
    do
    {
        PyObject* py_result;
        PyObject* dum;
        py_result = Py_CompileString(command.c_str(), "<stdin>", Py_single_input);
        dum = PyEval_EvalCode ((PyCodeObject *)py_result, glb, loc);
        Py_XDECREF (dum);
        Py_XDECREF (py_result);
        res = GetResultString( m_threadState );
        GetResultString( m_threadState ) = "";
        ++i;
        command = boost::str(
        boost::format("sys.completer.complete('%1%', %2%)\n")
            % hint
            % i);
        if (res.size())
        {
            // throw away the newline
            res = res.substr(1, res.size() - 3);
            m_suggestions.push_back(res);
        }
    }
    while (res.size());

    PyEval_ReleaseThread( m_threadState );
    return m_suggestions;
}

void
Interpreter::Initialize( )
{
    Py_Initialize( );
    PyEval_InitThreads( );
    MainThreadState = PyEval_SaveThread( );
}

void
Interpreter::Finalize( )
{
    PyEval_RestoreThread( MainThreadState );
    Py_Finalize( );
}

std::string& Interpreter::GetResultString( PyThreadState* threadState )
{
    static std::map< PyThreadState*, std::string > ResultStrings;
    if ( !ResultStrings.count( threadState ) )
    {
        ResultStrings[ threadState ] = "";
    }
    return ResultStrings[ threadState ];
}

PyObject* Interpreter::RedirectorInit(PyObject *, PyObject *)
{
    Py_INCREF(Py_None);
    return Py_None;
}

PyObject* Interpreter::RedirectorWrite(PyObject *, PyObject *args)
{
    char* output;
    PyObject *selfi;

    if (!PyArg_ParseTuple(args,"Os",&selfi,&output))
    {
        return NULL;
    }

    std::string outputString( output );
    PyThreadState* currentThread = PyThreadState_Get( );
    std::string& resultString = GetResultString( currentThread );
    resultString = resultString + outputString;
    Py_INCREF(Py_None);
    return Py_None;
}

PyMethodDef Interpreter::ModuleMethods[] = { {NULL,NULL,0,NULL} };
PyMethodDef Interpreter::RedirectorMethods[] =
{
    {"__init__", Interpreter::RedirectorInit, METH_VARARGS,
     "initialize the stdout/err redirector"},
    {"write", Interpreter::RedirectorWrite, METH_VARARGS,
     "implement the write method to redirect stdout/err"},
    {NULL,NULL,0,NULL},
};

void Interpreter::SetupRedirector( PyThreadState* threadState )
{
    PyMethodDef *def;

    /* create a new module and class */
    PyObject *module = Py_InitModule("redirector", ModuleMethods);
    PyObject *moduleDict = PyModule_GetDict(module);
    PyObject *classDict = PyDict_New();
    PyObject *className = PyString_FromString("redirector");
    PyObject *fooClass = PyClass_New(NULL, classDict, className);
    PyDict_SetItemString(moduleDict, "redirector", fooClass);
    Py_DECREF(classDict);
    Py_DECREF(className);
    Py_DECREF(fooClass);

    /* add methods to class */
    for (def = RedirectorMethods; def->ml_name != NULL; def++) {
        PyObject *func = PyCFunction_New(def, NULL);
        PyObject *method = PyMethod_New(func, NULL, fooClass);
        PyDict_SetItemString(classDict, def->ml_name, method);
        Py_DECREF(func);
        Py_DECREF(method);
    }
}
