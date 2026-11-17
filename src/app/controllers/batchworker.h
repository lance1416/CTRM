// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QMetaObject>
#include <QObject>
#include <QThread>

#include <functional>
#include <utility>

namespace ctrm::app {

// Runs the steps of a batch on its own thread, so the UI thread never waits for the disk
// (spec 7). One job at a time; the jobs run in the order of the calls.
class BatchWorker
{
public:
    BatchWorker();
    ~BatchWorker(); // waits for the current job
    BatchWorker(const BatchWorker &) = delete;
    BatchWorker &operator=(const BatchWorker &) = delete;
    BatchWorker(BatchWorker &&) = delete;
    BatchWorker &operator=(BatchWorker &&) = delete;

    // Runs job on the worker thread. Then runs done(result) on the thread of context. The
    // context must own the worker: its destructor then waits for the job, and Qt drops the
    // posted done call of a deleted context.
    template <typename T>
    void run(std::function<T()> job, QObject *context, std::function<void(T)> done)
    {
        QMetaObject::invokeMethod(
                m_inThread,
                [job = std::move(job), context, done = std::move(done)] {
                    T result = job();
                    QMetaObject::invokeMethod(
                            context, [done, result = std::move(result)] { done(result); },
                            Qt::QueuedConnection);
                },
                Qt::QueuedConnection);
    }

    QThread *thread();

private:
    QThread m_thread;
    QObject *m_inThread;
};

} // namespace ctrm::app
