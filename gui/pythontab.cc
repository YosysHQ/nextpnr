#include "pythontab.h"
#include <QGridLayout>
#include "emb.h"
#include "pybindings.h"

PythonTab::PythonTab(QWidget *parent) : QWidget(parent)
{
    PyImport_ImportModule("emb");

    // Add text area for Python output and input line
    plainTextEdit = new QPlainTextEdit();
    plainTextEdit->setReadOnly(true);
    plainTextEdit->setMinimumHeight(100);
    QFont f("unexistent");
    f.setStyleHint(QFont::Monospace);
    plainTextEdit->setFont(f);

    plainTextEdit->setContextMenuPolicy(Qt::CustomContextMenu);
    QAction *clearAction = new QAction("Clear &buffer", this);
    clearAction->setStatusTip("Clears display buffer");
    connect(clearAction, SIGNAL(triggered()), this, SLOT(clearBuffer()));
    contextMenu = plainTextEdit->createStandardContextMenu();
    contextMenu->addSeparator();
    contextMenu->addAction(clearAction);
    connect(plainTextEdit, SIGNAL(customContextMenuRequested(const QPoint)),
            this, SLOT(showContextMenu(const QPoint)));

    lineEdit = new LineEditor();
    lineEdit->setMinimumHeight(30);
    lineEdit->setMaximumHeight(30);
    lineEdit->setFont(f);

    QGridLayout *mainLayout = new QGridLayout();
    mainLayout->addWidget(plainTextEdit, 0, 0);
    mainLayout->addWidget(lineEdit, 1, 0);
    setLayout(mainLayout);

    connect(lineEdit, SIGNAL(textLineInserted(QString)), this,
            SLOT(editLineReturnPressed(QString)));

    write = [this](std::string s) {
        plainTextEdit->moveCursor(QTextCursor::End);
        plainTextEdit->insertPlainText(s.c_str());
        plainTextEdit->moveCursor(QTextCursor::End);
    };
    emb::set_stdout(write);

    char buff[1024];
    sprintf(buff, "Python %s on %s\n", Py_GetVersion(), Py_GetPlatform());
    print(buff);
}

void PythonTab::print(std::string line)
{
    plainTextEdit->moveCursor(QTextCursor::End);
    plainTextEdit->insertPlainText(line.c_str());
    plainTextEdit->moveCursor(QTextCursor::End);
}

void handle_system_exit() { exit(-1); }

int PythonTab::executePython(std::string &command)
{
    PyObject *m, *d, *v;
    m = PyImport_AddModule("__main__");
    if (m == NULL)
        return -1;
    d = PyModule_GetDict(m);
    v = PyRun_StringFlags(command.c_str(),
                          (command.empty() ? Py_file_input : Py_single_input),
                          d, d, NULL);
    if (v == NULL) {
        PyObject *exception, *v, *tb;

        if (PyErr_ExceptionMatches(PyExc_SystemExit)) {
            handle_system_exit();
        }
        PyErr_Fetch(&exception, &v, &tb);
        if (exception == NULL)
            return 0;
        PyErr_NormalizeException(&exception, &v, &tb);
        if (tb == NULL) {
            tb = Py_None;
            Py_INCREF(tb);
        }
        PyException_SetTraceback(v, tb);
        if (exception == NULL)
            return 0;
        PyErr_Clear();

        PyObject *objectsRepresentation = PyObject_Str(v);
        std::string errorStr =
                PyUnicode_AsUTF8(objectsRepresentation) + std::string("\n");
        print(errorStr);
        Py_DECREF(objectsRepresentation);
        Py_XDECREF(exception);
        Py_XDECREF(v);
        Py_XDECREF(tb);
        return -1;
    }
    Py_DECREF(v);
    return 0;
}

void PythonTab::editLineReturnPressed(QString text)
{
    std::string input = text.toStdString();
    print(std::string(">>> " + input + "\n"));
    int error = executePython(input);
}

void PythonTab::showContextMenu(const QPoint &pt)
{
    contextMenu->exec(mapToGlobal(pt));
}

void PythonTab::clearBuffer() { plainTextEdit->clear(); }