#ifndef INFOTAB_H
#define INFOTAB_H

#include <QMenu>
#include <QPlainTextEdit>
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

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

NEXTPNR_NAMESPACE_END

#endif // INFOTAB_H
