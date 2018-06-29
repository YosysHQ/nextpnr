/**
python-console
Copyright (C) 2018  Alex Tsui

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

#ifndef PYCONSOLE_H
#define PYCONSOLE_H
#include <QColor>
#include <QTextEdit>
#include <QMimeData>
#include "ParseHelper.h"
#include "ParseListener.h"

class QWidget;
class QKeyEvent;

class PythonConsole : public QTextEdit, ParseListener
{
    Q_OBJECT

  public:
    PythonConsole(QWidget *parent = 0);

    void displayPrompt();
    void displayString(QString text);

  protected:
    // override QTextEdit
    virtual void keyPressEvent(QKeyEvent *e);

    virtual void handleReturnKeyPress();

    virtual void insertFromMimeData(const QMimeData *src);

    /**
    Handle a compilable chunk of Python user input.
    */
    virtual void parseEvent(const ParseMessage &message);

    QString getLine();
    bool cursorIsOnInputLine();
    bool inputLineIsEmpty();
    bool canBackspace();
    bool canGoLeft();
    void autocomplete();
    void previousHistory();
    void nextHistory();
    void moveCursorToEnd();

    static const QString PROMPT;
    static const QString MULTILINE_PROMPT;

    static const QColor NORMAL_COLOR;
    static const QColor ERROR_COLOR;
    static const QColor OUTPUT_COLOR;

    ParseHelper m_parseHelper;
    std::list<std::string> m_historyBuffer;
    std::list<std::string>::const_iterator m_historyIt;
};

#endif // PYCONSOLE_H
