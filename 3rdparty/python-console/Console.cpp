#include "Console.h"
#include "Interpreter.h"
#include "ColumnFormatter.h"

#include <iostream>
#include <QKeyEvent>
#include <QFont>

#include "Utils.h"

const QString Console::PROMPT = ">>> ";
const QString Console::MULTILINE_PROMPT = "... ";
const QColor Console::NORMAL_COLOR = QColor::fromRgbF( 0, 0, 0 );
const QColor Console::ERROR_COLOR = QColor::fromRgbF( 1.0, 0, 0 );
const QColor Console::OUTPUT_COLOR = QColor::fromRgbF( 0, 0, 1.0 );

Console::Console( QWidget* parent ):
    QTextEdit( parent ),
    m_interpreter( new Interpreter )
{
    QFont font;
    font.setFamily("Courier New");
    setFont(font);
    m_parseHelper.subscribe( this );
    displayPrompt( );
}

Console::~Console( )
{
    delete m_interpreter;
}

void Console::keyPressEvent( QKeyEvent* e )
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

void Console::handleReturnKeyPress( )
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

void Console::parseEvent( const ParseMessage& message )
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
        res = m_interpreter->interpret( message.message, &errorCode );
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

QString Console::getLine( )
{
    QTextCursor cursor = textCursor();
    cursor.movePosition( QTextCursor::StartOfLine );
    cursor.movePosition( QTextCursor::Right, QTextCursor::MoveAnchor, Console::PROMPT.size( ) );
    cursor.movePosition( QTextCursor::EndOfLine, QTextCursor::KeepAnchor );
    QString line = cursor.selectedText( );
    cursor.clearSelection( );
    return line;
}

bool Console::cursorIsOnInputLine( )
{
    int cursorBlock = textCursor( ).blockNumber( );
    QTextCursor bottomCursor = textCursor( );
    bottomCursor.movePosition( QTextCursor::End );
    int bottomBlock = bottomCursor.blockNumber( );
    return ( cursorBlock == bottomBlock );
}

bool Console::inputLineIsEmpty( )
{
    QTextCursor bottomCursor = textCursor( );
    bottomCursor.movePosition( QTextCursor::End );
    int col = bottomCursor.columnNumber( );
    return ( col == Console::PROMPT.size( ) );
}

bool Console::canBackspace( )
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

bool Console::canGoLeft( )
{
    if ( cursorIsOnInputLine( ) )
    {
        QTextCursor bottomCursor = textCursor( );
        int col = bottomCursor.columnNumber( );
        return (col > Console::PROMPT.size( ));
    }
    return true;
}

void Console::displayPrompt( )
{
    QTextCursor cursor = textCursor();
    cursor.movePosition( QTextCursor::End );
    if ( m_parseHelper.buffered( ) )
    {
        cursor.insertText( Console::MULTILINE_PROMPT );
    }
    else
    {
        cursor.insertText( Console::PROMPT );
    }
    cursor.movePosition( QTextCursor::EndOfLine );
}

void Console::autocomplete( )
{
    if ( ! cursorIsOnInputLine( ) )
        return;

    QString line = getLine( );
    const std::list<std::string>& suggestions =
        m_interpreter->suggest( line.toStdString( ) );
    if (suggestions.size() == 1)
    {
        line = suggestions.back().c_str();
    }
    else
    {
        // try to complete to longest common prefix
        std::string prefix =
            LongestCommonPrefix(suggestions.begin(), suggestions.end());
        if (prefix.size() > line.size())
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

void Console::previousHistory( )
{
    if ( ! cursorIsOnInputLine( ) )
        return;

    if ( ! m_historyBuffer.size( ) )
        return;

    QTextCursor cursor = textCursor();
    cursor.movePosition( QTextCursor::StartOfLine );
    cursor.movePosition( QTextCursor::Right, QTextCursor::MoveAnchor, Console::PROMPT.size( ) );
    cursor.movePosition( QTextCursor::EndOfLine, QTextCursor::KeepAnchor );
    cursor.removeSelectedText( );
    if ( m_historyIt != m_historyBuffer.begin( ) )
    {
        --m_historyIt;
    }
    cursor.insertText( m_historyIt->c_str() );
}

void Console::nextHistory( )
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
    cursor.movePosition( QTextCursor::Right, QTextCursor::MoveAnchor, Console::PROMPT.size( ) );
    cursor.movePosition( QTextCursor::EndOfLine, QTextCursor::KeepAnchor );
    cursor.removeSelectedText( );
    ++m_historyIt;
    if ( m_historyIt == m_historyBuffer.end( ) )
    {
        return;
    }
    cursor.insertText( m_historyIt->c_str() );
}

void Console::moveCursorToEnd( )
{
    QTextCursor cursor = textCursor();
    cursor.movePosition( QTextCursor::End );
    setTextCursor( cursor );
}
