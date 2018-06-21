#ifndef WORKER_H
#define WORKER_H

#include "nextpnr.h"
#include <QThread>

// FIXME
USING_NEXTPNR_NAMESPACE

class Worker : public QObject
{
    Q_OBJECT
public:
    Worker(Context *ctx);
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
public Q_SLOTS:
    void info(const std::string &text);
Q_SIGNALS:
    void parsejson(const std::string &);
    void log(const std::string &text);
};

#endif // WORKER_H
