/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Miodrag Milanovic <miodrag@symbioticeda.com>
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

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
    explicit Worker(TaskManager *parent);
  public Q_SLOTS:
    void newContext(Context *);
    void pack();
    void budget(double freq);
    void place(bool timing_driven);
    void route();
  Q_SIGNALS:
    void log(const std::string &text);
    void pack_finished(bool status);
    void budget_finish(bool status);
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
    explicit TaskManager();
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
    void contextChanged(Context *ctx);
    void terminate();
    void pack();
    void budget(double freq);
    void place(bool timing_driven);
    void route();

    // redirected signals
    void log(const std::string &text);
    void pack_finished(bool status);
    void budget_finish(bool status);
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
