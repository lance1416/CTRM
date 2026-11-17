// SPDX-License-Identifier: GPL-3.0-or-later
#include "fakefilesystem.h"
#include "preview/foldercache.h"
#include "preview/previewscheduler.h"
#include "rules/insertrule.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QSignalSpy>
#include <QThread>
#include <QTimer>
#include <QtTest>

#include <atomic>
#include <memory>

using namespace ctrm;
using namespace ctrm::app;

namespace {

// Counts calls and records the thread of the last call. Adds "x" to the stem.
struct Probe
{
    std::atomic<int> calls{0};
    std::atomic<QThread *> thread{nullptr};
    int delayUs = 0; // busy wait in each call
};

class ProbeRule : public Rule
{
public:
    explicit ProbeRule(Probe *probe)
        : m_probe(probe)
    {}
    QString type() const override { return QStringLiteral("probe"); }
    std::unique_ptr<Rule> clone() const override { return std::make_unique<ProbeRule>(*this); }
    QStringList optionKeys() const override { return {}; }
    QJsonObject optionsToJson() const override { return {}; }
    void optionsFromJson(const QJsonObject &, QStringList *) override {}

protected:
    QString transform(const QString &text, const RuleContext &) const override
    {
        m_probe->calls.fetch_add(1);
        if (m_probe->delayUs > 0) {
            QElapsedTimer t;
            t.start();
            while (t.nsecsElapsed() < m_probe->delayUs * 1000LL) {
            }
        }
        m_probe->thread.store(QThread::currentThread());
        return text + QLatin1Char('x');
    }

private:
    Probe *m_probe;
};

// A FakeFileSystem that counts folder reads.
class CountingFileSystem : public FakeFileSystem
{
public:
    std::optional<QList<DirEntry>> listFolder(const QString &folder, QString *error) override
    {
        ++reads[folder];
        return FakeFileSystem::listFolder(folder, error);
    }
    QHash<QString, int> reads;
};

// count items "f<n>.txt" in folder "/f", with the probe rule. The FolderInfo is made
// directly: FakeFileSystem::addFile is slow for very large folders.
PreviewInput makeInput(int count, Probe *probe)
{
    PreviewInput in;
    in.os = OsRules::Linux;
    FolderInfo folder;
    folder.path = QStringLiteral("/f");
    folder.readable = true;
    folder.volume = {CaseSensitivity::Sensitive, false, IdentityType::Strong,
                     QStringLiteral("fakefs")};
    for (int i = 0; i < count; ++i) {
        const QString name = QStringLiteral("f%1.txt").arg(i);
        folder.names.append(name);
        Item it;
        it.id = ItemId(i + 1);
        it.folder = folder.path;
        it.name = name;
        in.items.append(it);
    }
    in.folders.insert(folder.path, folder);
    in.chain.insert(0, std::make_unique<ProbeRule>(probe));
    return in;
}

quint64 generationOf(const QList<QVariant> &signalArgs)
{
    return signalArgs.at(1).value<quint64>();
}

} // namespace

class TestPreview : public QObject
{
    Q_OBJECT
private slots:
    void smallIsSynchronous();
    void largeUsesWorker();
    void staleResultDropped();
    void cancelsRunningJob();
    void syncRequestDropsWorkerResult();
    void folderCacheReadsOnce();
    void uiNotBlocked();
};

void TestPreview::smallIsSynchronous()
{
    Probe probe;
    PreviewScheduler s;
    QSignalSpy ready(&s, &PreviewScheduler::previewReady);
    s.request(makeInput(10, &probe));
    QCOMPARE(ready.count(), 1);
    QCOMPARE(generationOf(ready.at(0)), s.currentGeneration());
    const auto result = ready.at(0).at(0).value<PreviewResult>();
    QCOMPARE(result.items.size(), 10);
    QCOMPARE(result.items[0].newName, QStringLiteral("f0x.txt"));
    QCOMPARE(probe.thread.load(), QThread::currentThread());
    QVERIFY(!s.isBusy());
}

void TestPreview::largeUsesWorker()
{
    Probe probe;
    PreviewScheduler s;
    QSignalSpy ready(&s, &PreviewScheduler::previewReady);
    s.request(makeInput(3000, &probe));
    QCOMPARE(ready.count(), 0);
    QVERIFY(s.isBusy());
    QVERIFY(ready.wait(10000));
    QCOMPARE(ready.count(), 1);
    QVERIFY(probe.thread.load() != QThread::currentThread());
    QCOMPARE(ready.at(0).at(0).value<PreviewResult>().items.size(), 3000);
    QVERIFY(!s.isBusy());
}

void TestPreview::staleResultDropped()
{
    Probe probe;
    PreviewScheduler s;
    QSignalSpy ready(&s, &PreviewScheduler::previewReady);
    const PreviewInput in = makeInput(50000, &probe);
    s.request(in);
    s.request(in);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 30000);
    QCOMPARE(generationOf(ready.at(0)), s.currentGeneration());
    QVERIFY(!s.isBusy());
    QTest::qWait(200);
    QCOMPARE(ready.count(), 1);
}

void TestPreview::cancelsRunningJob()
{
    Probe probe;
    PreviewScheduler s;
    QSignalSpy ready(&s, &PreviewScheduler::previewReady);
    probe.delayUs = 20; // about 1 s for each full run
    const PreviewInput in = makeInput(50000, &probe);
    s.request(in);
    // Wait until the first job runs, then request again.
    QTRY_VERIFY_WITH_TIMEOUT(probe.calls.load() > 0, 10000);
    s.request(in);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 30000);
    QVERIFY(!ready.at(0).at(0).value<PreviewResult>().cancelled);
    // The first job stopped early: fewer calls than two full runs.
    QVERIFY2(probe.calls.load() < 2 * 50000, qPrintable(QString::number(probe.calls.load())));
}

void TestPreview::syncRequestDropsWorkerResult()
{
    Probe probe;
    PreviewScheduler s;
    QSignalSpy ready(&s, &PreviewScheduler::previewReady);
    s.request(makeInput(3000, &probe));
    s.request(makeInput(5, &probe));
    QCOMPARE(ready.count(), 1);
    QCOMPARE(ready.at(0).at(0).value<PreviewResult>().items.size(), 5);
    QTRY_VERIFY_WITH_TIMEOUT(!s.isBusy(), 10000);
    QTest::qWait(100);
    QCOMPARE(ready.count(), 1);
}

void TestPreview::folderCacheReadsOnce()
{
    CountingFileSystem fs;
    fs.addFolder(QStringLiteral("/a"));
    fs.addFolder(QStringLiteral("/b"));
    fs.addFile(QStringLiteral("/a/1"));
    fs.addFile(QStringLiteral("/a/2"));
    fs.addFile(QStringLiteral("/b/3"));
    QList<Item> items;
    for (const auto &[folder, name] : {std::pair{"/a", "1"}, {"/a", "2"}, {"/b", "3"}}) {
        Item it;
        it.folder = QString::fromLatin1(folder);
        it.name = QString::fromLatin1(name);
        items.append(it);
    }
    FolderCache cache(fs);
    QHash<QString, FolderInfo> f = cache.folders(items);
    QCOMPARE(f.size(), 2);
    QVERIFY(f.value(QStringLiteral("/a")).readable);
    QCOMPARE(f.value(QStringLiteral("/a")).names.size(), 2);
    f = cache.folders(items);
    QCOMPARE(fs.reads.value(QStringLiteral("/a")), 1);
    QCOMPARE(fs.reads.value(QStringLiteral("/b")), 1);
    // Only the folders of the items.
    QCOMPARE(cache.folders(items.mid(2)).size(), 1);
    cache.invalidate();
    cache.folders(items);
    QCOMPARE(fs.reads.value(QStringLiteral("/a")), 2);
    QCOMPARE(fs.reads.value(QStringLiteral("/b")), 2);
}

void TestPreview::uiNotBlocked()
{
#ifndef NDEBUG
    QSKIP("timing test runs in release builds only");
#endif
    Probe probe;
    PreviewScheduler s;
    QSignalSpy ready(&s, &PreviewScheduler::previewReady);
    QElapsedTimer clock;
    qint64 last = 0;
    qint64 maxGap = 0;
    QTimer timer;
    timer.setInterval(10);
    connect(&timer, &QTimer::timeout, &timer, [&] {
        const qint64 now = clock.elapsed();
        maxGap = std::max(maxGap, now - last);
        last = now;
    });
    const PreviewInput in = makeInput(10000, &probe);
    clock.start();
    timer.start();
    s.request(in);
    QVERIFY(ready.wait(10000));
    timer.stop();
    QVERIFY2(maxGap <= 100, qPrintable(QStringLiteral("gap %1 ms").arg(maxGap)));
}

QTEST_MAIN(TestPreview)
#include "tst_app_preview.moc"
