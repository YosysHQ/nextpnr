#ifndef PYTHONTAB_H
#define PYTHONTAB_H

#include <QLineEdit>
#include <QPlainTextEdit>
#include "emb.h"
#include "line_editor.h"
#include "nextpnr.h"

// FIXME
USING_NEXTPNR_NAMESPACE

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

  private:
    QPlainTextEdit *plainTextEdit;
    LineEditor *lineEdit;
    emb::stdout_write_type write;
};

#endif // PYTHONTAB_H
