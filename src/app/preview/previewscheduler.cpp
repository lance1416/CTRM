// SPDX-License-Identifier: GPL-3.0-or-later
#include "preview/previewscheduler.h"

#include <QMetaObject>

#include <utility>

namespace ctrm::app {

PreviewScheduler::PreviewScheduler(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<ctrm::PreviewResult>();
    m_thread.setObjectName(QStringLiteral("ctrm-preview"));
    m_worker = new QObject;
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.start();
}

PreviewScheduler::~PreviewScheduler()
{
    cancelRunning();
    m_thread.quit();
    m_thread.wait();
}

void PreviewScheduler::cancelRunning()
{
    if (m_cancel)
        m_cancel->store(true);
    m_cancel.reset();
}

void PreviewScheduler::request(PreviewInput input)
{
    const quint64 generation = ++m_generation;
    cancelRunning();

    if (input.items.size() <= kWorkerThreshold) {
        m_busy = false;
        const PreviewResult result = computePreview(input);
        emit previewReady(result, generation);
        return;
    }

    m_busy = true;
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    m_cancel = cancel;
    // The destructor waits for the worker thread, so this object outlives each job. Qt drops
    // the queued result when this object is gone.
    QMetaObject::invokeMethod(
            m_worker,
            [this, cancel, generation, in = std::move(input)] {
                // An old job that was cancelled before it started returns at once.
                PreviewResult result = computePreview(in, cancel.get());
                if (result.cancelled)
                    return;
                QMetaObject::invokeMethod(
                        this,
                        [this, generation, r = std::move(result)] { finished(r, generation); },
                        Qt::QueuedConnection);
            },
            Qt::QueuedConnection);
}

void PreviewScheduler::finished(const PreviewResult &result, quint64 generation)
{
    if (generation != m_generation)
        return; // a newer request exists
    m_busy = false;
    m_cancel.reset();
    emit previewReady(result, generation);
}

quint64 PreviewScheduler::currentGeneration() const
{
    return m_generation;
}

bool PreviewScheduler::isBusy() const
{
    return m_busy;
}

} // namespace ctrm::app
