#ifndef INFOTAB_H
#define INFOTAB_H

#include <QMenu>
#include <QPlainTextEdit>
#include "nextpnr.h"

// FIXME
USING_NEXTPNR_NAMESPACE

class InfoTab : public QWidget
{
    Q_OBJECT

  public:
    explicit InfoTab(QWidget *parent = 0);
    void info(std::string str);
  private Q_SLOTS:
    void showContextMenu(const QPoint &pt);
    void clearBuffer();

  private:
    QPlainTextEdit *plainTextEdit;
    QMenu *contextMenu;
};

#endif // INFOTAB_H
