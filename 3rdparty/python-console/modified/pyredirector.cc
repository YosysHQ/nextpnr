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

#include "pyredirector.h"
#include <map>

static std::map<PyThreadState *, std::string> thread_strings;

static std::string &redirector_string(PyThreadState *threadState)
{
    if (!thread_strings.count(threadState)) {
        thread_strings[threadState] = "";
    }
    return thread_strings[threadState];
}

std::string redirector_take_output(PyThreadState *threadState)
{
    std::string res = redirector_string(threadState);
    redirector_string(threadState) = "";
    return res;
}

static PyObject *redirector_init(PyObject *, PyObject *)
{
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *redirector_write(PyObject *, PyObject *args)
{
    char *output;
    PyObject *selfi;

    if (!PyArg_ParseTuple(args, "Os", &selfi, &output)) {
        return NULL;
    }

    std::string outputString(output);
    PyThreadState *currentThread = PyThreadState_Get();
    std::string &resultString = redirector_string(currentThread);
    resultString = resultString + outputString;
    Py_INCREF(Py_None);
    return Py_None;
}

static PyMethodDef redirector_methods[] = {
        {"__init__", redirector_init, METH_VARARGS, "initialize the stdout/err redirector"},
        {"write", redirector_write, METH_VARARGS, "implement the write method to redirect stdout/err"},
        {NULL, NULL, 0, NULL},
};

static PyObject *createClassObject(const char *name, PyMethodDef methods[])
{
    PyObject *pClassName = PyUnicode_FromString(name);
    PyObject *pClassBases = PyTuple_New(0); // An empty tuple for bases is equivalent to `(object,)`
    PyObject *pClassDic = PyDict_New();

    PyMethodDef *def;
    // add methods to class
    for (def = methods; def->ml_name != NULL; def++) {
        PyObject *func = PyCFunction_New(def, NULL);
        PyObject *method = PyInstanceMethod_New(func);
        PyDict_SetItemString(pClassDic, def->ml_name, method);
        Py_DECREF(func);
        Py_DECREF(method);
    }

    // pClass = type(pClassName, pClassBases, pClassDic)
    PyObject *pClass = PyObject_CallFunctionObjArgs((PyObject *)&PyType_Type, pClassName, pClassBases, pClassDic, NULL);

    Py_DECREF(pClassName);
    Py_DECREF(pClassBases);
    Py_DECREF(pClassDic);

    return pClass;
}

static struct PyModuleDef moduledef = {PyModuleDef_HEAD_INIT, "redirector", 0, -1, 0};

PyMODINIT_FUNC PyInit_redirector(void)
{
    PyObject *m = PyModule_Create(&moduledef);
    if (m) {
        PyObject *fooClass = createClassObject("redirector", redirector_methods);
        PyModule_AddObject(m, "redirector", fooClass);
        Py_DECREF(fooClass);
    }
    return m;
}

void inittab_redirector() { PyImport_AppendInittab("redirector", PyInit_redirector); }