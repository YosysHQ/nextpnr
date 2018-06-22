#ifndef WORKER_H
#define WORKER_H

#include <QMutex>
#include <QThread>
#include "nextpnr.h"

// FIXME
USING_NEXTPNR_NAMESPACE

class TaskManager;

class Worker : public QObject
{
    Q_OBJECT
  public:
    Worker(Context *ctx, TaskManager *parent);
  public Q_SLOTS:
    void parsejson(const std::string &filename);
  Q_SIGNALS:
    void log(const std::string &text);

  private:
    Context *ctx;
};

class TaskManager : public QObject
{
    Q_OBJECT
    QThread workerThread;

  public:
    TaskManager(Context *ctx);
    ~TaskManager();
    bool shouldTerminate();
    void clearTerminate();
  public Q_SLOTS:
    void info(const std::string &text);
    void terminate_thread();
  Q_SIGNALS:
    void terminate();
    void parsejson(const std::string &);
    void log(const std::string &text);

  private:
    QMutex mutex;
    bool toTerminate;
};

#endif // WORKER_H
