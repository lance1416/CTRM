// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "preview/previewengine.h"

#include <QObject>
#include <QThread>

#include <atomic>
#include <memory>

namespace ctrm::app {

// Runs the preview. Small lists run at once on the calling thread; large lists run on one
// worker thread (spec 7). Only the result of the newest request is emitted.
class PreviewScheduler : public QObject
{
    Q_OBJECT
public:
    explicit PreviewScheduler(QObject *parent = nullptr);
    ~PreviewScheduler() override;

    // Each request gets the next generation. At most kWorkerThreshold items: computes now and
    // emits before it returns. More: cancels the running job and starts a job on the worker.
    void request(ctrm::PreviewInput input);
    quint64 currentGeneration() const;
    // True while a worker job of the current generation runs.
    bool isBusy() const;

    static constexpr int kWorkerThreshold = 2000;

signals:
    // Never emitted for an old generation or a cancelled result.
    void previewReady(const ctrm::PreviewResult &result, quint64 generation);

private:
    void finished(const ctrm::PreviewResult &result, quint64 generation);
    void cancelRunning();

    QThread m_thread;
    QObject *m_worker = nullptr; // lives in m_thread
    std::shared_ptr<std::atomic<bool>> m_cancel;
    quint64 m_generation = 0;
    bool m_busy = false;
};

} // namespace ctrm::app

Q_DECLARE_METATYPE(ctrm::PreviewResult)
