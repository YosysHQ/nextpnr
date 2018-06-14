#ifndef PYTHONTAB_H
#define PYTHONTAB_H

#include "nextpnr.h"
#include "emb.h"
#include <QLineEdit>
#include <QPlainTextEdit>

// FIXME
USING_NEXTPNR_NAMESPACE

class PythonTab : public QWidget
{
    Q_OBJECT

  public:
    explicit PythonTab(QWidget *parent = 0);

  private:
    int executePython(std::string command);
  private Q_SLOTS:
    void editLineReturnPressed();

  private:
    QPlainTextEdit *plainTextEdit;
    QLineEdit *lineEdit;
    emb::stdout_write_type write;
};

#endif // PYTHONTAB_H
