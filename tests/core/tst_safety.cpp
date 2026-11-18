// SPDX-License-Identifier: GPL-3.0-or-later
// Safety acceptance tests (PRD 10, AC12, AC13) on real folders with the native file system.
#include "exec/batchservice.h"
#include "fs/folderinfo.h"
#include "fs/itemsource.h"
#include "rules/changecaserule.h"
#include "rules/insertrule.h"
#include "rules/numberingrule.h"
#include "rules/replacerule.h"
#include "temptree.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include <atomic>
#include <cerrno>
#include <filesystem>
#include <functional>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#else
#include <unistd.h>
#endif
#ifdef Q_OS_MACOS
#include <sys/xattr.h>
#endif

using namespace ctrm;

namespace {

const AppInfo kApp{QStringLiteral("test"), {}};

// Calls a hook before each rename. When the hook gives a result, the rename is not done.
class HookFileSystem final : public FileSystem
{
public:
    using Hook = std::function<std::optional<RenameResult>(int call, const QString &from,
                                                           const QString &to)>;
    HookFileSystem(FileSystem &inner, Hook hook)
        : m_inner(inner)
        , m_hook(std::move(hook))
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
        if (auto r = m_hook(m_calls++, from, to))
            return *r;
        return m_inner.renameNoReplace(from, to);
    }

private:
    FileSystem &m_inner;
    Hook m_hook;
    int m_calls = 0;
};

// macOS keeps the extended attributes of a file in a "._<name>" file (AppleDouble) on volumes
// without extended attributes (exFAT, FAT, some SMB shares). It makes one for each new file.
bool isSidecar(const QString &name)
{
    return name.startsWith(QStringLiteral("._"));
}

QStringList allNamesIn(const QString &dir)
{
    return QDir(dir).entryList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Name);
}

// Exact names in a folder, sorted. A "._<name>" file is not in the list when "<name>" is: an
// orphan sidecar stays in the list, so that the comparison fails.
QStringList namesIn(const QString &dir)
{
    QStringList names = allNamesIn(dir);
    const QStringList all = names;
    names.removeIf([&](const QString &n) { return isSidecar(n) && all.contains(n.mid(2)); });
    return names;
}

// Paths (relative to root) of "._<name>" files whose "<name>" is not in the same folder with the
// exact same name. The sidecar must follow each rename and each undo.
QStringList orphanSidecars(const QString &root)
{
    QStringList orphans;
    QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                    QDirIterator::Subdirectories);
    QStringList dirs{root};
    while (it.hasNext())
        dirs.append(it.next());
    for (const QString &dir : dirs) {
        const QStringList names = allNamesIn(dir);
        for (const QString &n : names)
            if (isSidecar(n) && !names.contains(n.mid(2)))
                orphans.append(QDir(root).relativeFilePath(dir + u'/' + n));
    }
    return orphans;
}

// Gives the item an extended attribute with the tag (macOS only; elsewhere no effect). On a
// volume without extended attributes, macOS keeps it in the "._<name>" sidecar, so the tag shows
// if the sidecar follows the rename.
void setTag(const QString &path, const QByteArray &tag)
{
#ifdef Q_OS_MACOS
    if (setxattr(QFile::encodeName(path).constData(), "org.ctrm.test", tag.constData(),
                 size_t(tag.size()), 0, XATTR_NOFOLLOW) != 0)
        qFatal("setxattr failed: %s", qPrintable(path));
#else
    Q_UNUSED(path);
    Q_UNUSED(tag);
#endif
}

// The tag of setTag(). On other systems: the expected value, so the check always passes.
QByteArray tagOf(const QString &path, const QByteArray &expected)
{
#ifdef Q_OS_MACOS
    Q_UNUSED(expected);
    char buffer[64];
    const ssize_t n = getxattr(QFile::encodeName(path).constData(), "org.ctrm.test", buffer,
                               sizeof buffer, 0, XATTR_NOFOLLOW);
    return n < 0 ? QByteArray("<none>") : QByteArray(buffer, int(n));
#else
    Q_UNUSED(path);
    return expected;
#endif
}

// Checksums without the sidecar files.
QMap<QString, QByteArray> mainChecksums(const TempTree &tree)
{
    QMap<QString, QByteArray> sums = tree.checksums();
    sums.removeIf([](const auto &it) { return isSidecar(QFileInfo(it.key()).fileName()); });
    return sums;
}

bool hasTempName(const QStringList &names)
{
    for (const QString &n : names)
        if (n.startsWith(QStringLiteral(".ctrm-tmp-")))
            return true;
    return false;
}

QByteArray contentOf(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<missing>");
}

// One request per (oldPath, newName) pair, with the identity type of the volume.
Plan planFor(FileSystem &fs, const QList<QPair<QString, QString>> &renames,
             const QSet<int> &folders = {})
{
    QList<RenameRequest> requests;
    ItemId id = 1;
    for (const auto &[path, newName] : renames) {
        RenameRequest r;
        r.item = id++;
        r.folder = path.left(path.lastIndexOf(u'/'));
        r.oldName = path.mid(path.lastIndexOf(u'/') + 1);
        r.newName = newName;
        r.isFolder = folders.contains(int(r.item));
        const auto identity = fs.identity(path, fs.volumeInfo(r.folder).identityType);
        if (!identity)
            qFatal("no identity: %s", qPrintable(path));
        r.identity = *identity;
        requests.append(r);
    }
    PlanResult result = makePlan(requests, fs, newBatchId());
    if (!result.plan)
        qFatal("makePlan failed: %s", qPrintable(result.error));
    return *result.plan;
}

struct Batch
{
    RunOutcome run;
    FinishResult finish;
};

// Runs a plan; complete when finished, else roll back.
Batch runBatch(BatchService &service, const Plan &plan, const QString &undoOf = {})
{
    QString error;
    auto s = service.start(plan, RuleChain(), undoOf, &error);
    if (!s)
        qFatal("start failed: %s", qPrintable(error));
    std::atomic<bool> cancel{false};
    Batch b;
    b.run = s->run(cancel);
    b.finish = b.run.finished ? s->complete() : s->rollBack();
    return b;
}

struct Env
{
    TempTree tree;
    QTemporaryDir historyDir;
    std::unique_ptr<FileSystem> fs = createNativeFileSystem();
    HistoryStore history{historyDir.path()};
    BatchService service{*fs, history, kApp};
    QString path(const QString &rel) const { return tree.root() + u'/' + rel; }
};

} // namespace

class TstSafety : public QObject
{
    Q_OBJECT

private slots:
    void swapContents();
    void threeCycle();
    void chainNoTemp();
    void caseOnly();
    void folderAndChild();
    void raceTargetAppears();
    void readOnlyFailureRollsBack();
    void killMidBatchRecover();
    void noTempNamesLeft();
    void timestampsUnchanged();
    void previewEqualsDisk();
    void undoWithChecksums();
    void noReplaceEverywhere();
    void symlinkRenamedNotTarget();
};

void TstSafety::swapContents()
{
    Env env;
    setTag(env.tree.file("a.txt", "A"), "tag-a");
    setTag(env.tree.file("b.txt", "B"), "tag-b");
    const auto before = env.tree.checksums();
    const Batch b = runBatch(env.service, planFor(*env.fs, {{env.path("a.txt"), "b.txt"},
                                                            {env.path("b.txt"), "a.txt"}}));
    QVERIFY(b.run.finished);
    QCOMPARE(b.finish.state, BatchState::Complete);
    const auto after = env.tree.checksums();
    QCOMPARE(after.value("a.txt"), before.value("b.txt"));
    QCOMPARE(after.value("b.txt"), before.value("a.txt"));
    QCOMPARE(namesIn(env.tree.root()), (QStringList{"a.txt", "b.txt"}));
    QCOMPARE(orphanSidecars(env.tree.root()), QStringList());
    // The extended attributes (on exFAT: the sidecars) follow the files.
    QCOMPARE(tagOf(env.path("b.txt"), "tag-a"), QByteArray("tag-a"));
    QCOMPARE(tagOf(env.path("a.txt"), "tag-b"), QByteArray("tag-b"));
}

void TstSafety::threeCycle()
{
    Env env;
    env.tree.file("1.txt", "1");
    env.tree.file("2.txt", "2");
    env.tree.file("3.txt", "3");
    const auto before = env.tree.checksums();
    const Batch b = runBatch(env.service, planFor(*env.fs, {{env.path("1.txt"), "2.txt"},
                                                            {env.path("2.txt"), "3.txt"},
                                                            {env.path("3.txt"), "1.txt"}}));
    QVERIFY(b.run.finished);
    const auto after = env.tree.checksums();
    QCOMPARE(after.value("2.txt"), before.value("1.txt"));
    QCOMPARE(after.value("3.txt"), before.value("2.txt"));
    QCOMPARE(after.value("1.txt"), before.value("3.txt"));
    QCOMPARE(mainChecksums(env.tree).size(), 3);
}

void TstSafety::chainNoTemp()
{
    Env env;
    env.tree.file("a.txt", "A");
    env.tree.file("b.txt", "B");
    const Plan plan =
            planFor(*env.fs, {{env.path("a.txt"), "b.txt"}, {env.path("b.txt"), "c.txt"}});
    QVERIFY(runBatch(env.service, plan).run.finished);
    QString error;
    const auto rec = env.history.load(plan.batchId, &error);
    QVERIFY2(rec, qPrintable(error));
    QCOMPARE(rec->steps.size(), 2);
    for (const PlanStep &s : rec->plan.steps)
        QCOMPARE(s.kind, StepKind::Direct);
    QCOMPARE(contentOf(env.path("b.txt")), QByteArray("A"));
    QCOMPARE(contentOf(env.path("c.txt")), QByteArray("B"));
}

void TstSafety::caseOnly()
{
    Env env;
    env.tree.file("a.txt", "A");
    const Batch b = runBatch(env.service, planFor(*env.fs, {{env.path("a.txt"), "A.txt"}}));
    QVERIFY(b.run.finished);
    QCOMPARE(namesIn(env.tree.root()), (QStringList{"A.txt"}));
    QCOMPARE(orphanSidecars(env.tree.root()), QStringList());
    QCOMPARE(contentOf(env.path("A.txt")), QByteArray("A"));
}

void TstSafety::folderAndChild()
{
    Env env;
    env.tree.file("d/f.txt", "F");
    const auto before = env.tree.checksums();
    const Batch b =
            runBatch(env.service,
                     planFor(*env.fs, {{env.path("d"), "e"}, {env.path("d/f.txt"), "g.txt"}}, {1}));
    QVERIFY(b.run.finished);
    QCOMPARE(namesIn(env.tree.root()), (QStringList{"e"}));
    QCOMPARE(env.tree.checksums().value("e/g.txt"), before.value("d/f.txt"));
}

void TstSafety::raceTargetAppears()
{
    Env env;
    env.tree.file("a.txt", "A");
    env.tree.file("b.txt", "B");
    const Plan plan =
            planFor(*env.fs, {{env.path("a.txt"), "x.txt"}, {env.path("b.txt"), "y.txt"}});
    // Another program makes y.txt just before the second rename.
    HookFileSystem fs(
            *env.fs,
            [&](int call, const QString &, const QString &to) -> std::optional<RenameResult> {
                if (call == 1) {
                    QFile f(to);
                    if (!f.open(QIODevice::NewOnly) || f.write("NEW") != 3)
                        qFatal("cannot make %s", qPrintable(to));
                }
                return std::nullopt;
            });
    BatchService service(fs, env.history, kApp);
    const Batch b = runBatch(service, plan);
    QCOMPARE(b.run.failedStep, 1);
    QCOMPARE(b.finish.state, BatchState::RolledBack);
    QCOMPARE(contentOf(env.path("y.txt")), QByteArray("NEW")); // not replaced
    QCOMPARE(contentOf(env.path("a.txt")), QByteArray("A"));
    QCOMPARE(contentOf(env.path("b.txt")), QByteArray("B"));
    QCOMPARE(namesIn(env.tree.root()), (QStringList{"a.txt", "b.txt", "y.txt"}));
}

void TstSafety::readOnlyFailureRollsBack()
{
#ifndef Q_OS_WIN
    if (geteuid() == 0)
        QSKIP("root can write to a read-only folder");
#endif
    Env env;
    env.tree.file("s1/a.txt", "A");
    env.tree.file("s1/b.txt", "B");
    const QString c = env.tree.file("s2/c.txt", "C");
    const auto before = env.tree.checksums();
    const Plan plan = planFor(
            *env.fs,
            {{env.path("s1/a.txt"), "x_a.txt"}, {env.path("s1/b.txt"), "x_b.txt"}, {c, "x_c.txt"}});
    QCOMPARE(plan.steps[2].item, ItemId(3));
#ifdef Q_OS_WIN
    // c.txt is open without FILE_SHARE_DELETE: its rename fails (sharing violation).
    const HANDLE h = CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(c).utf16()),
                                 GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    QVERIFY(h != INVALID_HANDLE_VALUE);
    const Batch b = runBatch(env.service, plan);
    CloseHandle(h);
#else
    // s2 is read-only: the rename of c.txt fails with EACCES.
    const QString s2 = env.path("s2");
    const auto perms = QFile::permissions(s2);
    QVERIFY(QFile::setPermissions(s2, QFileDevice::ReadOwner | QFileDevice::ExeOwner |
                                              QFileDevice::ReadGroup | QFileDevice::ExeGroup |
                                              QFileDevice::ReadOther | QFileDevice::ExeOther));
    if (QFileInfo(s2).isWritable()) {
        QFile::setPermissions(s2, perms);
        QSKIP("the volume does not keep file permissions (for example exFAT or FAT): "
              "a read-only folder is not possible");
    }
    const Batch b = runBatch(env.service, plan);
    QFile::setPermissions(s2, perms);
    QCOMPARE(b.run.failure.errorCode, EACCES);
#endif
    QCOMPARE(b.run.failedStep, 2);
    QVERIFY(!b.run.failure.ok);
    QCOMPARE(b.finish.state, BatchState::RolledBack);
    QCOMPARE(env.tree.checksums(), before);
}

void TstSafety::killMidBatchRecover()
{
    Env env;
    const QString folder = env.tree.mkdir("batch");
    for (int i = 0; i < 1000; ++i)
        env.tree.file(QStringLiteral("batch/f%1.txt").arg(i, 4, 10, QLatin1Char('0')),
                      QByteArray::number(i));
    const auto before = env.tree.checksums();

    // The helper stops after step 400 and waits: no time limit here for the speed of the steps.
    QProcess helper;
    helper.start(QStringLiteral(CTRM_CRASH_HELPER), {folder, env.historyDir.path(), "400"});
    QVERIFY2(helper.waitForStarted(), qPrintable(helper.errorString()));
    int step = 0;
    while (step < 400) {
        if (!helper.canReadLine() && !helper.waitForReadyRead(1000)) {
            if (helper.state() == QProcess::NotRunning)
                break;
            continue;
        }
        while (helper.canReadLine()) {
            const QByteArray line = helper.readLine().trimmed();
            if (line.startsWith("STEP "))
                step = line.mid(5).toInt();
        }
    }
    QVERIFY2(step >= 400, helper.readAllStandardError().constData());
    QCOMPARE(helper.state(), QProcess::Running); // in the middle of the batch
    helper.kill();
    QVERIFY(helper.waitForFinished());

    const QList<BatchSummary> pending = env.service.pendingRecovery();
    QCOMPARE(pending.size(), 1);
    QCOMPARE(pending.first().state, BatchState::InProgress);
    const FinishResult f = env.service.recoverRollBack(pending.first().batchId);
    QVERIFY2(f.error.isEmpty(), qPrintable(f.error));
    QCOMPARE(f.state, BatchState::RolledBack);
    QCOMPARE(namesIn(folder).size(), 1000);
    QVERIFY(!hasTempName(namesIn(folder)));
    QCOMPARE(env.tree.checksums(), before);
}

void TstSafety::noTempNamesLeft()
{
    Env env;
    env.tree.file("a.txt", "A");
    env.tree.file("b.txt", "B");
    const auto before = env.tree.checksums();
    // After success.
    QVERIFY(runBatch(env.service,
                     planFor(*env.fs, {{env.path("a.txt"), "b.txt"}, {env.path("b.txt"), "a.txt"}}))
                    .run.finished);
    QVERIFY(!hasTempName(namesIn(env.tree.root())));

    // After a roll back: the last step (temp -> new name) fails.
    const Plan plan =
            planFor(*env.fs, {{env.path("a.txt"), "b.txt"}, {env.path("b.txt"), "a.txt"}});
    HookFileSystem fs(
            *env.fs, [](int call, const QString &, const QString &) -> std::optional<RenameResult> {
                if (call != 2)
                    return std::nullopt;
                RenameResult r;
                r.errorCode = EIO;
                r.errorText = QStringLiteral("I/O error");
                return r;
            });
    BatchService service(fs, env.history, kApp);
    const Batch b = runBatch(service, plan);
    QCOMPARE(b.run.failedStep, 2);
    QCOMPARE(b.finish.state, BatchState::RolledBack);
    QVERIFY(!hasTempName(namesIn(env.tree.root())));
    const auto after = env.tree.checksums();
    QCOMPARE(after.value("a.txt"), before.value("b.txt")); // the state after the first batch
    QCOMPARE(after.value("b.txt"), before.value("a.txt"));
}

void TstSafety::timestampsUnchanged()
{
    Env env;
    const QString a = env.tree.file("a.txt", "A");
    const QString d = env.tree.mkdir("d");
    const QDateTime old(QDate(2020, 5, 6), QTime(7, 8, 9), QTimeZone::UTC);
    {
        QFile f(a);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.setFileTime(old, QFileDevice::FileModificationTime));
    }
    const QDateTime folderTime = QFileInfo(d).lastModified();
    const Batch b = runBatch(env.service, planFor(*env.fs, {{a, "x.txt"}, {d, "e"}}, {2}));
    QVERIFY(b.run.finished);
    QCOMPARE(QFileInfo(env.path("x.txt")).lastModified().toUTC(), old);
    QCOMPARE(QFileInfo(env.path("e")).lastModified(), folderTime);
}

void TstSafety::previewEqualsDisk()
{
    Env env;
    const QString folder = env.tree.mkdir("p");
    for (int i = 1; i <= 200; ++i)
        env.tree.file(QStringLiteral("p/file %1.txt").arg(i), QByteArray::number(i));

    PreviewInput in;
    in.items = folderChildren(*env.fs, folder, false).items;
    QCOMPARE(in.items.size(), 200);
    for (int i = 0; i < in.items.size(); ++i)
        in.items[i].id = ItemId(i + 1);
    in.folders.insert(folder, readFolderInfo(*env.fs, folder));
    auto replace = std::make_unique<ReplaceRule>();
    replace->find = QStringLiteral("file ");
    replace->replace = QStringLiteral("doc-");
    auto upper = std::make_unique<ChangeCaseRule>();
    upper->caseMode = ChangeCaseRule::Case::Upper;
    auto numbering = std::make_unique<NumberingRule>();
    numbering->position = NumberingRule::Position::Prefix;
    auto prefix = std::make_unique<InsertRule>();
    prefix->text = QStringLiteral("p_");
    auto suffix = std::make_unique<InsertRule>();
    suffix->text = QStringLiteral("_s");
    suffix->position = InsertRule::Position::Suffix;
    in.chain.insert(0, std::move(replace));
    in.chain.insert(1, std::move(upper));
    in.chain.insert(2, std::move(numbering));
    in.chain.insert(3, std::move(prefix));
    in.chain.insert(4, std::move(suffix));

    const PreviewResult preview = computePreview(in);
    QCOMPARE(preview.summary.willChange, 200);
    const PrepareResult prepared = env.service.prepare(in, preview);
    QCOMPARE(prepared.kind, PrepareResult::Kind::Ready);
    QString error;
    auto s = env.service.start(*prepared.plan, in.chain, {}, &error);
    QVERIFY(s);
    std::atomic<bool> cancel{false};
    QVERIFY(s->run(cancel).finished);
    QCOMPARE(s->complete().state, BatchState::Complete);

    QStringList expected;
    for (int i = 0; i < preview.items.size(); ++i) {
        const QString &newName = preview.items[i].newName;
        expected.append(newName);
        // Each file has the name the preview gave it (contents are "1" ... "200").
        const QString oldName = in.items[i].name; // "file <n>.txt", content "<n>"
        const QByteArray n = oldName.mid(5, oldName.size() - 9).toUtf8();
        QCOMPARE(contentOf(folder + u'/' + newName), n);
    }
    expected.sort();
    QCOMPARE(namesIn(folder), expected);
}

void TstSafety::undoWithChecksums()
{
    Env env;
    setTag(env.tree.file("a.txt", "A"), "tag-a");
    setTag(env.tree.file("b.txt", "B"), "tag-b");
    setTag(env.tree.file("d/c.txt", "C"), "tag-c");
    const auto before = mainChecksums(env.tree);
    const Plan plan = planFor(*env.fs,
                              {{env.path("a.txt"), "b.txt"},
                               {env.path("b.txt"), "a.txt"},
                               {env.path("d"), "e"},
                               {env.path("d/c.txt"), "z.txt"}},
                              {3});
    QVERIFY(runBatch(env.service, plan).run.finished);
    QVERIFY(mainChecksums(env.tree) != before);

    QString error;
    const UndoPreview p = env.service.previewUndo(plan.batchId, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(p.items.size(), 4);
    if (env.fs->volumeInfo(env.tree.root()).identityType == IdentityType::Weak) {
        // AC13.9: with a weak identity check, Quick Undo opens the undo preview.
        QVERIFY(p.weak());
        QVERIFY(!p.quickUndoAllowed());
        QCOMPARE(p.passCount, 4);
    } else {
        QVERIFY(p.quickUndoAllowed());
    }
    const PlanResult undoPlan = env.service.planUndo(p);
    QVERIFY(undoPlan.plan);
    const Batch u = runBatch(env.service, *undoPlan.plan, plan.batchId);
    QVERIFY(u.run.finished);
    QCOMPARE(mainChecksums(env.tree), before);
    QCOMPARE(orphanSidecars(env.tree.root()), QStringList());
    QCOMPARE(tagOf(env.path("a.txt"), "tag-a"), QByteArray("tag-a"));
    QCOMPARE(tagOf(env.path("b.txt"), "tag-b"), QByteArray("tag-b"));
    QCOMPARE(tagOf(env.path("d/c.txt"), "tag-c"), QByteArray("tag-c"));
    QCOMPARE(env.history.load(plan.batchId, &error)->state(), BatchState::Undone);
}

void TstSafety::noReplaceEverywhere()
{
    Env env;
    env.tree.file("a.txt", "A");
    env.tree.file("b.txt", "B");
    env.tree.mkdir("d");
    const auto before = env.tree.checksums();
    QVERIFY(!env.fs->renameNoReplace(env.path("a.txt"), env.path("b.txt")).ok);
    QVERIFY(!env.fs->renameNoReplace(env.path("a.txt"), env.path("d")).ok);
    QCOMPARE(env.tree.checksums(), before);
}

void TstSafety::symlinkRenamedNotTarget()
{
    Env env;
    const QString target = env.tree.file("target.txt", "T");
    const QString link = env.path("link");
    std::error_code ec;
    std::filesystem::create_symlink(std::filesystem::path(target.toStdU16String()),
                                    std::filesystem::path(link.toStdU16String()), ec);
    if (ec) {
#ifdef Q_OS_WIN
        QSKIP("Symbolic links are not permitted for this user");
#else
        QFAIL(ec.message().c_str());
#endif
    }
    QVERIFY(runBatch(env.service, planFor(*env.fs, {{link, "link2"}})).run.finished);
    QVERIFY(QFileInfo(env.path("link2")).isSymLink());
    QVERIFY(!QFileInfo::exists(link) && !QFileInfo(link).isSymLink());
    QCOMPARE(contentOf(target), QByteArray("T"));
    QCOMPARE(namesIn(env.tree.root()), (QStringList{"link2", "target.txt"}));
}

QTEST_GUILESS_MAIN(TstSafety)
#include "tst_safety.moc"
