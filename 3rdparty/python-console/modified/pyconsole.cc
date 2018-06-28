#include "pyconsole.h"
#include "pyinterpreter.h"
#include "ColumnFormatter.h"

#include <iostream>
#include <QKeyEvent>
#include <QFont>

#include "Utils.h"

const QString PythonConsole::PROMPT = ">>> ";
const QString PythonConsole::MULTILINE_PROMPT = "... ";
const QColor PythonConsole::NORMAL_COLOR = QColor::fromRgbF( 0, 0, 0 );
const QColor PythonConsole::ERROR_COLOR = QColor::fromRgbF( 1.0, 0, 0 );
const QColor PythonConsole::OUTPUT_COLOR = QColor::fromRgbF( 0, 0, 1.0 );

PythonConsole::PythonConsole( QWidget* parent ):
    QTextEdit( parent )
{
    QFont font("unexistent");
    font.setStyleHint(QFont::Monospace);
    setFont(font);
    m_parseHelper.subscribe( this );    
}

void PythonConsole::keyPressEvent( QKeyEvent* e )
{
    switch ( e->key() )
    {
        case Qt::Key_Return:
            handleReturnKeyPress( );
            return;

        case Qt::Key_Tab:
            autocomplete( );
            return;

        case Qt::Key_Backspace:
            if ( ! canBackspace( ) )
                return;
            break;

        case Qt::Key_Up:
            previousHistory( );
            return;

        case Qt::Key_Down:
            nextHistory( );
            return;

        case Qt::Key_Left:
            if ( ! canGoLeft( ) )
                return;
    }

    QTextEdit::keyPressEvent( e );
}

void PythonConsole::handleReturnKeyPress( )
{
    if ( ! cursorIsOnInputLine( ) )
    {
        return;
    }

    QString line = getLine( );

    m_parseHelper.process( line.toStdString( ) );
    if ( m_parseHelper.buffered( ) )
    {
        append("");
        displayPrompt( );
    }
    if ( line.size( ) )
    {
        m_historyBuffer.push_back( line.toStdString( ) );
        m_historyIt = m_historyBuffer.end();
    }
    moveCursorToEnd( );
}

void PythonConsole::parseEvent( const ParseMessage& message )
{
    // handle invalid user input
    if ( message.errorCode )
    {
        setTextColor( ERROR_COLOR );
        append(message.message.c_str());

        setTextColor( NORMAL_COLOR );
        append("");
        displayPrompt( );
        return;
    }

    // interpret valid user input
    int errorCode;
    std::string res;
    if ( message.message.size() )
        res = pyinterpreter_execute( message.message, &errorCode );
    if ( errorCode )
    {
        setTextColor( ERROR_COLOR );
    }
    else
    {
        setTextColor( OUTPUT_COLOR );
    }

    if ( res.size( ) )
    {
        append(res.c_str());
    }

    setTextColor( NORMAL_COLOR );

    // set up the next line on the console
    append("");
    displayPrompt( );
}

QString PythonConsole::getLine( )
{
    QTextCursor cursor = textCursor();
    cursor.movePosition( QTextCursor::StartOfLine );
    cursor.movePosition( QTextCursor::Right, QTextCursor::MoveAnchor, PythonConsole::PROMPT.size( ) );
    cursor.movePosition( QTextCursor::EndOfLine, QTextCursor::KeepAnchor );
    QString line = cursor.selectedText( );
    cursor.clearSelection( );
    return line;
}

bool PythonConsole::cursorIsOnInputLine( )
{
    int cursorBlock = textCursor( ).blockNumber( );
    QTextCursor bottomCursor = textCursor( );
    bottomCursor.movePosition( QTextCursor::End );
    int bottomBlock = bottomCursor.blockNumber( );
    return ( cursorBlock == bottomBlock );
}

bool PythonConsole::inputLineIsEmpty( )
{
    QTextCursor bottomCursor = textCursor( );
    bottomCursor.movePosition( QTextCursor::End );
    int col = bottomCursor.columnNumber( );
    return ( col == PythonConsole::PROMPT.size( ) );
}

bool PythonConsole::canBackspace( )
{
    if ( ! cursorIsOnInputLine( ) )
    {
        return false;
    }

    if ( inputLineIsEmpty( ) )
    {
        return false;
    }

    return true;
}

bool PythonConsole::canGoLeft( )
{
    if ( cursorIsOnInputLine( ) )
    {
        QTextCursor bottomCursor = textCursor( );
        int col = bottomCursor.columnNumber( );
        return (col > PythonConsole::PROMPT.size( ));
    }
    return true;
}

void PythonConsole::displayPrompt( )
{
    QTextCursor cursor = textCursor();
    cursor.movePosition( QTextCursor::End );
    if ( m_parseHelper.buffered( ) )
    {
        cursor.insertText( PythonConsole::MULTILINE_PROMPT );
    }
    else
    {
        cursor.insertText( PythonConsole::PROMPT );
    }
    cursor.movePosition( QTextCursor::EndOfLine );
}

void PythonConsole::displayString(QString text)
{
    QTextCursor cursor = textCursor();
    cursor.movePosition( QTextCursor::End );
    cursor.insertText( text );
    cursor.movePosition( QTextCursor::EndOfLine );
}

void PythonConsole::autocomplete( )
{
    if ( ! cursorIsOnInputLine( ) )
        return;

    QString line = getLine( );
    const std::list<std::string>& suggestions =
        pyinterpreter_suggest( line.toStdString( ) );
    if (suggestions.size() == 1)
    {
        line = suggestions.back().c_str();
    }
    else
    {
        // try to complete to longest common prefix
        std::string prefix =
            LongestCommonPrefix(suggestions.begin(), suggestions.end());
        if (prefix.size() > (size_t)line.size())
        {
            line = prefix.c_str();
        }
        else
        {
            ColumnFormatter fmt;
            fmt.setItems(suggestions.begin(), suggestions.end());
            fmt.format(width() / 10);
            setTextColor( OUTPUT_COLOR );
            const std::list<std::string>& formatted = fmt.formattedOutput();
            for (std::list<std::string>::const_iterator it = formatted.begin();
                it != formatted.end(); ++it)
            {
                append(it->c_str());
            }
            setTextColor( NORMAL_COLOR );
        }
    }

    // set up the next line on the console
    append("");
    displayPrompt( );
    moveCursorToEnd( );
    QTextCursor cursor = textCursor();
    cursor.insertText( line );
    moveCursorToEnd( );
}

void PythonConsole::previousHistory( )
{
    if ( ! cursorIsOnInputLine( ) )
        return;

    if ( ! m_historyBuffer.size( ) )
        return;

    QTextCursor cursor = textCursor();
    cursor.movePosition( QTextCursor::StartOfLine );
    cursor.movePosition( QTextCursor::Right, QTextCursor::MoveAnchor, PythonConsole::PROMPT.size( ) );
    cursor.movePosition( QTextCursor::EndOfLine, QTextCursor::KeepAnchor );
    cursor.removeSelectedText( );
    if ( m_historyIt != m_historyBuffer.begin( ) )
    {
        --m_historyIt;
    }
    cursor.insertText( m_historyIt->c_str() );
}

void PythonConsole::nextHistory( )
{
    if ( ! cursorIsOnInputLine( ) )
        return;

    if ( ! m_historyBuffer.size( ) )
        return;
    if ( m_historyIt == m_historyBuffer.end( ) )
    {
        return;
    }
    QTextCursor cursor = textCursor();
    cursor.movePosition( QTextCursor::StartOfLine );
    cursor.movePosition( QTextCursor::Right, QTextCursor::MoveAnchor, PythonConsole::PROMPT.size( ) );
    cursor.movePosition( QTextCursor::EndOfLine, QTextCursor::KeepAnchor );
    cursor.removeSelectedText( );
    ++m_historyIt;
    if ( m_historyIt == m_historyBuffer.end( ) )
    {
        return;
    }
    cursor.insertText( m_historyIt->c_str() );
}

void PythonConsole::moveCursorToEnd( )
{
    QTextCursor cursor = textCursor();
    cursor.movePosition( QTextCursor::End );
    setTextCursor( cursor );
}
