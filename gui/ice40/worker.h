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
    void loadfile(const std::string &);
    void pack();
    void place();
    void route();
  Q_SIGNALS:
    void log(const std::string &text);
    void loadfile_finished(bool status);
    void pack_finished(bool status);
    void place_finished(bool status);
    void route_finished(bool status);
    void taskCanceled();
    void taskStarted();
    void taskPaused();

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
    void loadfile(const std::string &);
    void pack();
    void place();
    void route();

    // redirected signals
    void log(const std::string &text);
    void loadfile_finished(bool status);
    void pack_finished(bool status);
    void place_finished(bool status);
    void route_finished(bool status);
    void taskCanceled();
    void taskStarted();
    void taskPaused();

  private:
    QMutex mutex;
    bool toTerminate;
    bool toPause;
};

NEXTPNR_NAMESPACE_END

#endif // WORKER_H
