#include "worker.h"
#include <fstream>
#include "bitstream.h"
#include "design_utils.h"
#include "jsonparse.h"
#include "log.h"
#include "pack.h"
#include "pcf.h"
#include "place_sa.h"
#include "route.h"
#include "timing.h"

struct WorkerInterruptionRequested
{
};

Worker::Worker(Context *_ctx, TaskManager *parent) : ctx(_ctx)
{
    log_write_function = [this, parent](std::string text) {
        Q_EMIT log(text);
        if (parent->shouldTerminate()) {
            parent->clearTerminate();
            throw WorkerInterruptionRequested();
        }
    };
}

void Worker::parsejson(const std::string &filename)
{
    std::string fn = filename;
    std::ifstream f(fn);
    try {
        if (!parse_json_file(f, fn, ctx))
            log_error("Loading design failed.\n");
        if (!pack_design(ctx))
            log_error("Packing design failed.\n");
        double freq = 50e6;
        assign_budget(ctx, freq);
        print_utilisation(ctx);

        if (!place_design_sa(ctx))
            log_error("Placing design failed.\n");
        if (!route_design(ctx))
            log_error("Routing design failed.\n");
        Q_EMIT log("DONE\n");
    } catch (log_execution_error_exception) {
    } catch (WorkerInterruptionRequested) {
        Q_EMIT log("CANCELED\n");
    }
}

TaskManager::TaskManager(Context *ctx) : toTerminate(false)
{
    Worker *worker = new Worker(ctx, this);
    worker->moveToThread(&workerThread);
    connect(&workerThread, &QThread::finished, worker, &QObject::deleteLater);
    connect(this, &TaskManager::parsejson, worker, &Worker::parsejson);
    connect(worker, &Worker::log, this, &TaskManager::info);
    workerThread.start();
}

TaskManager::~TaskManager()
{
    if (workerThread.isRunning()) 
        terminate_thread();
    workerThread.quit();
    workerThread.wait();
}

void TaskManager::info(const std::string &result) { Q_EMIT log(result); }

void TaskManager::terminate_thread()
{
    QMutexLocker locker(&mutex);
    toTerminate = true;
}

bool TaskManager::shouldTerminate()
{
    QMutexLocker locker(&mutex);
    return toTerminate;
}

void TaskManager::clearTerminate()
{
    QMutexLocker locker(&mutex);
    toTerminate = false;
}