#ifndef LINE_EDITOR_H
#define LINE_EDITOR_H

#include <QLineEdit>
#include <QMenu>
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

class LineEditor : public QLineEdit
{
    Q_OBJECT

  public:
    explicit LineEditor(QWidget *parent = 0);

  private Q_SLOTS:
    void textInserted();
    void showContextMenu(const QPoint &pt);
    void clearHistory();

  Q_SIGNALS:
    void textLineInserted(QString);

  protected:
    void keyPressEvent(QKeyEvent *) Q_DECL_OVERRIDE;

  private:
    int index;
    QStringList lines;
    QMenu *contextMenu;
};

NEXTPNR_NAMESPACE_END

#endif // LINE_EDITOR_H
