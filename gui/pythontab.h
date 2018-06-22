#ifndef PYTHONTAB_H
#define PYTHONTAB_H

#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include "emb.h"
#include "line_editor.h"
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

class PythonTab : public QWidget
{
    Q_OBJECT

  public:
    explicit PythonTab(QWidget *parent = 0);

  private:
    void print(std::string line);
    int executePython(std::string &command);
  private Q_SLOTS:
    void editLineReturnPressed(QString text);
    void showContextMenu(const QPoint &pt);
    void clearBuffer();

  private:
    QPlainTextEdit *plainTextEdit;
    LineEditor *lineEdit;
    QMenu *contextMenu;
    emb::stdout_write_type write;
};

NEXTPNR_NAMESPACE_END

#endif // PYTHONTAB_H
