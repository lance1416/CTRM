// SPDX-License-Identifier: GPL-3.0-or-later
// ctrm_crash_helper <folder> <historyDir> <delayMs>
// Makes f0000.txt ... f0999.txt in <folder> when missing, then renames them with the prefix
// "x_" through BatchService. Sleeps <delayMs> before each rename and prints "STEP <n>" after
// each step, so that a test can kill the process in the middle of the batch.
#include "exec/batchservice.h"
#include "fs/folderinfo.h"
#include "fs/itemsource.h"
#include "rules/insertrule.h"

#include <QCoreApplication>
#include <QFile>
#include <QThread>

#include <atomic>
#include <cstdio>

using namespace ctrm;

namespace {

class SlowFileSystem final : public FileSystem
{
public:
    SlowFileSystem(FileSystem &inner, int delayMs)
        : m_inner(inner)
        , m_delayMs(delayMs)
    {}
    VolumeInfo volumeInfo(const QString &folder) override { return m_inner.volumeInfo(folder); }
    std::optional<QList<DirEntry>> listFolder(const QString &folder, QString *error) override
    {
        return m_inner.listFolder(folder, error);
    }
    bool exists(const QString &path) override { return m_inner.exists(path); }
    std::optional<FileIdentity> identity(const QString &path, IdentityType type) override
    {
        return m_inner.identity(path, type);
    }
    RenameResult renameNoReplace(const QString &from, const QString &to) override
    {
        QThread::msleep(m_delayMs);
        return m_inner.renameNoReplace(from, to);
    }

private:
    FileSystem &m_inner;
    int m_delayMs;
};

int fail(const char *what, const QString &detail)
{
    std::fprintf(stderr, "%s: %s\n", what, qPrintable(detail));
    return 1;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() != 4)
        return fail("usage", "ctrm_crash_helper <folder> <historyDir> <delayMs>");
    const QString folder = args[1];
    const int delayMs = args[3].toInt();

    for (int i = 0; i < 1000; ++i) {
        QFile f(folder + QStringLiteral("/f%1.txt").arg(i, 4, 10, QLatin1Char('0')));
        if (f.exists())
            continue;
        if (!f.open(QIODevice::WriteOnly) || f.write(QByteArray::number(i)) < 0)
            return fail("cannot write", f.fileName());
    }

    const std::unique_ptr<FileSystem> native = createNativeFileSystem();
    SlowFileSystem fs(*native, delayMs);
    HistoryStore history(args[2]);
    BatchService service(fs, history, AppInfo{QStringLiteral("test"), {}});

    PreviewInput in;
    in.items = folderChildren(fs, folder, false).items;
    for (int i = 0; i < in.items.size(); ++i)
        in.items[i].id = ItemId(i + 1);
    in.folders.insert(folder, readFolderInfo(fs, folder));
    auto rule = std::make_unique<InsertRule>();
    rule->text = QStringLiteral("x_");
    in.chain.insert(0, std::move(rule));

    const PrepareResult prepared = service.prepare(in, computePreview(in));
    if (prepared.kind != PrepareResult::Kind::Ready)
        return fail("prepare", prepared.error);
    QString error;
    auto session = service.start(*prepared.plan, in.chain, {}, &error);
    if (!session)
        return fail("start", error);
    std::atomic<bool> cancel{false};
    const RunOutcome out = session->run(cancel, [](int done, int) {
        std::printf("STEP %d\n", done);
        std::fflush(stdout);
    });
    if (!out.finished)
        return fail("run", out.failure.errorText);
    session->complete();
    return 0;
}
