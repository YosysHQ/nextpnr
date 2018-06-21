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

Worker::Worker(Context *_ctx) : ctx(_ctx)
{
    log_write_function = [this](std::string text) { Q_EMIT log(text); };
}

void Worker::parsejson(const std::string &filename)
{
    std::string fn = filename;
    std::ifstream f(fn);

    parse_json_file(f, fn, ctx);
    if (!pack_design(ctx))
        log_error("Packing design failed.\n");
    double freq = 50e6;
    assign_budget(ctx, freq);
    print_utilisation(ctx);

    if (!place_design_sa(ctx))
        log_error("Placing design failed.\n");
    if (!route_design(ctx))
        log_error("Routing design failed.\n");
    print_utilisation(ctx);
    Q_EMIT log("done");
}

TaskManager::TaskManager(Context *ctx)
{
    Worker *worker = new Worker(ctx);
    worker->moveToThread(&workerThread);
    connect(&workerThread, &QThread::finished, worker, &QObject::deleteLater);
    connect(this, &TaskManager::parsejson, worker, &Worker::parsejson);
    connect(worker, &Worker::log, this, &TaskManager::info);
    workerThread.start();
}

TaskManager::~TaskManager()
{
    workerThread.quit();
    workerThread.wait();
}

void TaskManager::info(const std::string &result) { Q_EMIT log(result); }