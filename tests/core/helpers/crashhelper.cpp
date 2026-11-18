// SPDX-License-Identifier: GPL-3.0-or-later
// ctrm_crash_helper <folder> <historyDir> <stopStep>
// Renames each file in <folder> with the prefix "x_" through BatchService. Prints "STEP <n>" after
// each step. After step <stopStep> it waits in the middle of the batch until the test kills it: the
// test does not depend on the speed of the steps (each step syncs the journal, which is slow on a
// loaded machine).
#include "exec/batchservice.h"
#include "fs/folderinfo.h"
#include "fs/itemsource.h"
#include "rules/insertrule.h"

#include <QCoreApplication>
#include <QThread>

#include <atomic>
#include <cstdio>

using namespace ctrm;

namespace {

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
        return fail("usage", "ctrm_crash_helper <folder> <historyDir> <stopStep>");
    const QString folder = args[1];
    const int stopStep = args[3].toInt();

    const std::unique_ptr<FileSystem> native = createNativeFileSystem();
    FileSystem &fs = *native;
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
    const RunOutcome out = session->run(cancel, [stopStep](int done, int) {
        std::printf("STEP %d\n", done);
        std::fflush(stdout);
        if (done >= stopStep) {
            for (;;)
                QThread::sleep(1);
        }
    });
    if (!out.finished)
        return fail("run", out.failure.errorText);
    session->complete();
    return 0;
}
