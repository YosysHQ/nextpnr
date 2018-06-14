#include "infotab.h"
#include <QGridLayout>

InfoTab::InfoTab(QWidget *parent) : QWidget(parent)
{
    plainTextEdit = new QPlainTextEdit();
    plainTextEdit->setReadOnly(true);

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

