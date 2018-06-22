#ifndef WORKER_H
#define WORKER_H

#include <QMutex>
#include <QThread>
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

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
    bool isPaused();
  public Q_SLOTS:
    void info(const std::string &text);
    void terminate_thread();
    void pause_thread();
    void continue_thread();
  Q_SIGNALS:
    void terminate();
    void parsejson(const std::string &);
    void log(const std::string &text);

  private:
    QMutex mutex;
    bool toTerminate;
    bool toPause;
};

NEXTPNR_NAMESPACE_END

#endif // WORKER_H
