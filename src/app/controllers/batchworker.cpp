// SPDX-License-Identifier: GPL-3.0-or-later
#include "batchworker.h"

namespace ctrm::app {

BatchWorker::BatchWorker()
    : m_inThread(new QObject)
{
    m_thread.setObjectName(QStringLiteral("ctrm-batch"));
    m_inThread->moveToThread(&m_thread);
    m_thread.start();
}

BatchWorker::~BatchWorker()
{
    m_thread.quit();
    m_thread.wait();
    delete m_inThread;
}

QThread *BatchWorker::thread()
{
    return &m_thread;
}

} // namespace ctrm::app
