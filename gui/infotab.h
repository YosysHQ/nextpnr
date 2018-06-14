#ifndef INFOTAB_H
#define INFOTAB_H

#include "nextpnr.h"
#include <QPlainTextEdit>

// FIXME
USING_NEXTPNR_NAMESPACE

class InfoTab : public QWidget
{
    Q_OBJECT

  public:
    explicit InfoTab(QWidget *parent = 0);
    void info(std::string str);

  private:
    QPlainTextEdit *plainTextEdit;
};

#endif // INFOTAB_H
