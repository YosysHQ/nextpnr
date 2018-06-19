#include "infotab.h"
#include <QGridLayout>

InfoTab::InfoTab(QWidget *parent) : QWidget(parent)
{
    plainTextEdit = new QPlainTextEdit();
    plainTextEdit->setReadOnly(true);
    QFont f("unexistent");
    f.setStyleHint(QFont::Monospace);
    plainTextEdit->setFont(f);

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
