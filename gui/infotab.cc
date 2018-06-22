#include "infotab.h"
#include <QGridLayout>

NEXTPNR_NAMESPACE_BEGIN

InfoTab::InfoTab(QWidget *parent) : QWidget(parent)
{
    plainTextEdit = new QPlainTextEdit();
    plainTextEdit->setReadOnly(true);
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

    QGridLayout *mainLayout = new QGridLayout();
    mainLayout->addWidget(plainTextEdit);
    setLayout(mainLayout);
}

void InfoTab::info(std::string str)
{
    plainTextEdit->moveCursor(QTextCursor::End);
    plainTextEdit->insertPlainText(str.c_str());
    plainTextEdit->moveCursor(QTextCursor::End);
}

void InfoTab::showContextMenu(const QPoint &pt)
{
    contextMenu->exec(mapToGlobal(pt));
}

void InfoTab::clearBuffer() { plainTextEdit->clear(); }

NEXTPNR_NAMESPACE_END
