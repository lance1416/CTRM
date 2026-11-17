// SPDX-License-Identifier: GPL-3.0-or-later
#include "exec/historystore.h"
#include "exec/journal.h"
#include "temptree.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimeZone>
#include <QUuid>
#include <QtTest>

#ifndef Q_OS_WIN
#include <csignal>
#include <sys/resource.h>
#endif

using namespace ctrm;

namespace {

QDateTime utc(int y, int mo, int d, int h = 12, int mi = 0, int s = 0, int ms = 0)
{
    return QDateTime(QDate(y, mo, d), QTime(h, mi, s, ms), QTimeZone::UTC);
}

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

Plan samplePlan(const QString &batchId, const QStringList &folders = {"/a"})
{
    Plan p;
    p.batchId = batchId;
    ItemId id = 7;
    for (const QString &folder : folders) {
        RenameRequest r;
        r.item = id;
        r.folder = folder;
        r.oldName = "x";
        r.newName = "y";
        r.identity.type = IdentityType::Strong;
        r.identity.volume = 1;
        r.identity.idLow = 18446744073709551615ULL; // 64-bit value stays exact
        p.requests.append(r);
        PlanStep s;
        s.index = int(p.steps.size());
        s.item = id;
        s.from = folder + "/x";
        s.to = folder + "/y";
        s.kind = StepKind::Direct;
        p.steps.append(s);
        ++id;
    }
    return p;
}

JournalHeader sampleHeader(const QString &batchId, const QDateTime &started,
                           const QString &undoOf = {})
{
    JournalHeader h;
    h.batchId = batchId;
    h.started = started;
    h.ctrmVersion = "0.1.0";
    h.os = "macOS 15.1";
    h.chain = QJsonObject{{"name", "Test"}, {"rules", QJsonArray()}};
    h.undoOf = undoOf;
    return h;
}

// Writes a journal file with header and state lines only, without fsync (fast, for many files).
QString writeRaw(const QString &dir, const QString &batchId, const QDateTime &started,
                 BatchState state, const QString &undoOf = {}, const QStringList &folders = {"/a"})
{
    QJsonArray requests;
    for (const QString &f : folders) {
        requests.append(QJsonObject{
                {"item", "1"},
                {"folder", f},
                {"oldName", "x"},
                {"newName", "y"},
                {"isFolder", false},
                {"identity", identityToJson(FileIdentity{})},
        });
    }
    const QJsonObject header{
            {"type", "header"},
            {"format", "ctrm-journal"},
            {"formatVersion", 1},
            {"batchId", batchId},
            {"started", started.toString(Qt::ISODateWithMs)},
            {"ctrmVersion", "0.1.0"},
            {"os", "test"},
            {"chain", QJsonObject()},
            {"undoOf", undoOf.isEmpty() ? QJsonValue() : QJsonValue(undoOf)},
            {"requests", requests},
            {"steps", QJsonArray()},
    };
    QByteArray data = QJsonDocument(header).toJson(QJsonDocument::Compact) + '\n';
    const auto stateLine = [&](BatchState s) {
        data += QJsonDocument(QJsonObject{{"type", "state"},
                                          {"state", batchStateToString(s)},
                                          {"time", started.toString(Qt::ISODateWithMs)}})
                        .toJson(QJsonDocument::Compact) +
                '\n';
    };
    stateLine(BatchState::InProgress);
    if (state != BatchState::InProgress)
        stateLine(state);
    const QString path =
            dir + "/" + started.toString("yyyyMMdd'T'HHmmsszzz'Z'") + "-" + batchId + ".jsonl";
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size())
        qFatal("cannot write %s", qPrintable(path));
    return path;
}

void appendBytes(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::Append));
    QCOMPARE(f.write(bytes), bytes.size());
}

StepRecord okRecord(int step, Phase phase, const QDateTime &time, const QString &fallback = {})
{
    StepRecord r;
    r.step = step;
    r.phase = phase;
    r.ok = true;
    r.fallback = fallback;
    r.time = time;
    return r;
}

} // namespace

class TestJournal : public QObject
{
    Q_OBJECT
private slots:
    void stateStrings();
    void writeAndRead();
    void eachLineValidJson();
    void ignoresTruncatedTail();
    void ignoresInvalidLastLine();
    void badMiddleLineFails();
    void bestEffortSkipsBadMiddleLine();
    void writeFailureLeavesNoPartialLine();
    void notAJournalFails();
    void fileNameHasNoColon();
    void createNeverReplaces();
    void appendAfterReopen();
    void appendAfterTruncatedTail();
    void startRecordsOpen();
    void openAndReadAgree_data();
    void openAndReadAgree();
    void bestEffortOpenWithoutHeader();
    void listNewestFirst();
    void listShowsDamaged();
    void inProgressListed();
    void undoBatchesOf();
    void loadAndAppendStateById();
    void retentionKeepsByCountOrAge();
    void retentionKeepsUnsafe();
    void clearKeepsUnsafe();
    void retentionDeletesGroups();
    void fileNameTime();
    void damagedListTimeWithoutMs();
};

void TestJournal::stateStrings()
{
    const QList<QPair<BatchState, QString>> rows = {
            {BatchState::InProgress, "in-progress"},     {BatchState::Complete, "complete"},
            {BatchState::RolledBack, "rolled-back"},     {BatchState::Incomplete, "incomplete"},
            {BatchState::PartlyUndone, "partly-undone"}, {BatchState::Undone, "undone"},
    };
    for (const auto &[state, text] : rows) {
        QCOMPARE(batchStateToString(state), text);
        QCOMPARE(batchStateFromString(text), std::optional<BatchState>(state));
    }
    QVERIFY(!batchStateFromString("Complete"));
    QVERIFY(!batchStateFromString(""));
}

void TestJournal::writeAndRead()
{
    TempTree t;
    const QString id = newId();
    const QDateTime started = utc(2026, 11, 17, 14, 2, 0, 123);
    const Plan plan = samplePlan(id, {"/a", "/b"});
    QString error;
    auto w = JournalWriter::create(t.root() + "/history", sampleHeader(id, started), plan, &error);
    QVERIFY2(w, qPrintable(error));
    QVERIFY(w->appendStep(plan.steps[0], okRecord(0, Phase::Forward, started.addMSecs(5))));
    StepRecord failed;
    failed.step = 1;
    failed.phase = Phase::Forward;
    failed.ok = false;
    failed.errorCode = 13;
    failed.errorText = "Permission denied";
    failed.time = started.addMSecs(6);
    QVERIFY(w->appendStep(plan.steps[1], failed));
    QVERIFY(w->appendStep(plan.steps[0],
                          okRecord(0, Phase::Rollback, started.addMSecs(7), "link-unlink")));
    QVERIFY(w->appendState({BatchState::Complete, started.addSecs(1), {}}));

    const auto r = readJournal(w->filePath(), &error);
    QVERIFY2(r, qPrintable(error));
    QVERIFY(!r->ignoredTail);
    QCOMPARE(r->filePath, w->filePath());
    QCOMPARE(r->header.batchId, id);
    QCOMPARE(r->header.started, started);
    QCOMPARE(r->header.ctrmVersion, QStringLiteral("0.1.0"));
    QCOMPARE(r->header.os, QStringLiteral("macOS 15.1"));
    QCOMPARE(r->header.chain, sampleHeader(id, started).chain);
    QVERIFY(r->header.undoOf.isEmpty());
    QCOMPARE(r->plan.batchId, id);
    QCOMPARE(r->plan.requests.size(), 2);
    QCOMPARE(r->plan.requests[1].item, ItemId(8));
    QCOMPARE(r->plan.requests[1].folder, QStringLiteral("/b"));
    QCOMPARE(r->plan.requests[0].identity, plan.requests[0].identity);
    QCOMPARE(r->plan.steps.size(), 2);
    QCOMPARE(r->plan.steps[1].from, QStringLiteral("/b/x"));
    QCOMPARE(r->plan.steps[1].kind, StepKind::Direct);
    QCOMPARE(r->steps.size(), 3);
    QVERIFY(r->steps[0].ok);
    QCOMPARE(r->steps[0].time, started.addMSecs(5));
    QVERIFY(!r->steps[1].ok);
    QCOMPARE(r->steps[1].errorCode, 13);
    QCOMPARE(r->steps[1].errorText, QStringLiteral("Permission denied"));
    QCOMPARE(r->steps[2].phase, Phase::Rollback);
    QCOMPARE(r->steps[2].fallback, QStringLiteral("link-unlink"));
    QCOMPARE(r->states.size(), 2);
    QCOMPARE(r->states[0].state, BatchState::InProgress);
    QCOMPARE(r->states[0].time, started);
    QCOMPARE(r->state(), BatchState::Complete);
    QCOMPARE(r->ended(), started.addSecs(1));

    // The rollback record holds the reverse rename.
    QFile f(w->filePath());
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QList<QByteArray> lines = f.readAll().split('\n');
    const QJsonObject rollback = QJsonDocument::fromJson(lines[4]).object();
    QCOMPARE(rollback.value("from").toString(), QStringLiteral("/a/y"));
    QCOMPARE(rollback.value("to").toString(), QStringLiteral("/a/x"));
    QCOMPARE(QJsonDocument::fromJson(lines[0]).object().value("requests")[0]["item"].toString(),
             QStringLiteral("7"));
}

void TestJournal::eachLineValidJson()
{
    TempTree t;
    const QString id = newId();
    const Plan plan = samplePlan(id);
    QString error;
    auto w = JournalWriter::create(t.root(), sampleHeader(id, utc(2026, 1, 1)), plan, &error);
    QVERIFY2(w, qPrintable(error));
    QVERIFY(w->appendStep(plan.steps[0], okRecord(0, Phase::Forward, utc(2026, 1, 1))));
    QVERIFY(w->appendState({BatchState::Complete, utc(2026, 1, 1), {}}));
    QFile f(w->filePath());
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray data = f.readAll();
    QVERIFY(data.endsWith('\n'));
    const QList<QByteArray> lines = data.trimmed().split('\n');
    QCOMPARE(lines.size(), 4);
    for (const QByteArray &line : lines) {
        QJsonParseError e;
        QVERIFY(QJsonDocument::fromJson(line, &e).isObject());
        QCOMPARE(e.error, QJsonParseError::NoError);
    }
    QCOMPARE(QJsonDocument::fromJson(lines[0]).object().value("type").toString(),
             QStringLiteral("header"));
    QCOMPARE(QJsonDocument::fromJson(lines[1]).object().value("state").toString(),
             QStringLiteral("in-progress"));
}

void TestJournal::ignoresTruncatedTail()
{
    TempTree t;
    const QString path = writeRaw(t.root(), newId(), utc(2026, 1, 1), BatchState::InProgress);
    appendBytes(path, "{\"type\":\"op\",\"st");
    QString error;
    const auto r = readJournal(path, &error);
    QVERIFY2(r, qPrintable(error));
    QVERIFY(r->ignoredTail);
    QCOMPARE(r->state(), BatchState::InProgress);
    QVERIFY(r->steps.isEmpty());
}

void TestJournal::ignoresInvalidLastLine()
{
    TempTree t;
    const QString path = writeRaw(t.root(), newId(), utc(2026, 1, 1), BatchState::Complete);
    appendBytes(path, "{\"type\":\"op\",\"st\n");
    QString error;
    const auto r = readJournal(path, &error);
    QVERIFY2(r, qPrintable(error));
    QVERIFY(r->ignoredTail);
    QCOMPARE(r->state(), BatchState::Complete);
}

void TestJournal::badMiddleLineFails()
{
    TempTree t;
    const QString path = writeRaw(t.root(), newId(), utc(2026, 1, 1), BatchState::InProgress);
    appendBytes(path, "not json\n{\"type\":\"state\",\"state\":\"complete\",\"time\":\"x\"}\n");
    QString error;
    QVERIFY(!readJournal(path, &error));
    QVERIFY(!error.isEmpty());
}

void TestJournal::bestEffortSkipsBadMiddleLine()
{
    TempTree t;
    const QString path = writeRaw(t.root(), newId(), utc(2026, 1, 1), BatchState::InProgress);
    appendBytes(path, "not json\n{\"type\":\"op\",\"step\":99}\n"
                      "{\"type\":\"state\",\"state\":\"complete\",\"time\":\"x\"}\n");
    QString error;
    QVERIFY(!readJournal(path, &error));
    QVERIFY(!readJournal(path, &error, ReadMode::Strict));
    const auto r = readJournal(path, &error, ReadMode::BestEffort);
    QVERIFY2(r, qPrintable(error));
    QCOMPARE(r->skippedLines, (QList<int>{3, 4})); // 1-based line numbers
    QCOMPARE(r->state(), BatchState::Complete);
    QVERIFY(!r->ignoredTail);

    // The header is never skipped.
    QVERIFY(!readJournal(t.file("h.jsonl", "garbage\n{\"type\":\"state\"}\n"), &error,
                         ReadMode::BestEffort));
    // A strict read has no skipped lines.
    const QString clean = writeRaw(t.root(), newId(), utc(2026, 1, 2), BatchState::Complete);
    QVERIFY(readJournal(clean, &error, ReadMode::BestEffort)->skippedLines.isEmpty());
}

void TestJournal::writeFailureLeavesNoPartialLine()
{
#ifdef Q_OS_WIN
    QSKIP("Uses RLIMIT_FSIZE to make a short write (POSIX only)");
#else
    TempTree t;
    const QString id = newId();
    QString error;
    auto w = JournalWriter::create(t.root(), sampleHeader(id, utc(2026, 1, 1)), samplePlan(id),
                                   &error);
    QVERIFY2(w, qPrintable(error));
    const qint64 size = QFileInfo(w->filePath()).size();

    // The OS accepts only 10 more bytes of the file: the record is a short write.
    rlimit old{};
    QCOMPARE(getrlimit(RLIMIT_FSIZE, &old), 0);
    const auto oldHandler = std::signal(SIGXFSZ, SIG_IGN);
    rlimit low = old;
    low.rlim_cur = rlim_t(size + 10);
    QCOMPARE(setrlimit(RLIMIT_FSIZE, &low), 0);
    PlanStep step = samplePlan(id).steps.first();
    const bool written = w->appendStep(step, okRecord(0, Phase::Forward, utc(2026, 1, 1)));
    setrlimit(RLIMIT_FSIZE, &old);
    std::signal(SIGXFSZ, oldHandler);

    QVERIFY(!written);
    QCOMPARE(QFileInfo(w->filePath()).size(), size); // the part of the line is removed
    const auto r = readJournal(w->filePath(), &error);
    QVERIFY2(r, qPrintable(error));
    QVERIFY(!r->ignoredTail);
    QVERIFY(r->steps.isEmpty());

    // The next record starts on its own line.
    QVERIFY(w->appendState({BatchState::RolledBack, utc(2026, 1, 2), {}}));
    const auto r2 = readJournal(w->filePath(), &error);
    QVERIFY2(r2, qPrintable(error));
    QCOMPARE(r2->state(), BatchState::RolledBack);
#endif
}

void TestJournal::notAJournalFails()
{
    TempTree t;
    QString error;
    QVERIFY(!readJournal(t.file("a.jsonl", "{\"type\":\"state\"}\n"), &error));
    QVERIFY(!readJournal(t.file("b.jsonl", ""), &error));
    QVERIFY(!readJournal(t.file("c.jsonl", "{\"type\":\"head"), &error));
    QVERIFY(!readJournal(t.root() + "/missing.jsonl", &error));
}

void TestJournal::fileNameHasNoColon()
{
    TempTree t;
    const QString id = newId();
    QString error;
    auto w = JournalWriter::create(t.root(), sampleHeader(id, utc(2026, 11, 17, 14, 2, 0, 5)),
                                   samplePlan(id), &error);
    QVERIFY2(w, qPrintable(error));
    const QString name = QFileInfo(w->filePath()).fileName();
    QVERIFY(!name.contains(u':'));
    QVERIFY(name.endsWith("-" + id + ".jsonl"));
    QCOMPARE(name, "20261117T140200005Z-" + id + ".jsonl");
}

void TestJournal::createNeverReplaces()
{
    TempTree t;
    const QString id = newId();
    QString error;
    QVERIFY(JournalWriter::create(t.root(), sampleHeader(id, utc(2026, 1, 1)), samplePlan(id),
                                  &error));
    QVERIFY(!JournalWriter::create(t.root(), sampleHeader(id, utc(2026, 1, 1)), samplePlan(id),
                                   &error));
    QVERIFY(!error.isEmpty());
}

void TestJournal::appendAfterReopen()
{
    TempTree t;
    const QString id = newId();
    QString error;
    QString path;
    {
        auto w = JournalWriter::create(t.root(), sampleHeader(id, utc(2026, 1, 1)), samplePlan(id),
                                       &error);
        QVERIFY2(w, qPrintable(error));
        path = w->filePath();
    }
    auto w = JournalWriter::openForAppend(path, &error);
    QVERIFY2(w, qPrintable(error));
    QVERIFY(w->appendState({BatchState::RolledBack, utc(2026, 1, 2), {}}));
    const auto r = readJournal(path, &error);
    QVERIFY2(r, qPrintable(error));
    QCOMPARE(r->state(), BatchState::RolledBack);
    QCOMPARE(r->states.size(), 2);
    QVERIFY(!JournalWriter::openForAppend(t.root() + "/missing.jsonl", &error));
}

void TestJournal::appendAfterTruncatedTail()
{
    TempTree t;
    const QString path = writeRaw(t.root(), newId(), utc(2026, 1, 1), BatchState::InProgress);
    appendBytes(path, "{\"type\":\"op\",\"st");
    QString error;
    auto w = JournalWriter::openForAppend(path, &error);
    QVERIFY2(w, qPrintable(error));
    QVERIFY(w->appendState({BatchState::RolledBack, utc(2026, 1, 2), {}}));
    const auto r = readJournal(path, &error);
    QVERIFY2(r, qPrintable(error));
    QVERIFY(!r->ignoredTail);
    QCOMPARE(r->state(), BatchState::RolledBack);

    // A last line with '\n' that is not valid JSON is also cut off.
    const QString path2 = writeRaw(t.root(), newId(), utc(2026, 1, 3), BatchState::InProgress);
    appendBytes(path2, "{\"type\":\"op\",\"st\n");
    auto w2 = JournalWriter::openForAppend(path2, &error);
    QVERIFY2(w2, qPrintable(error));
    QVERIFY(w2->appendState({BatchState::Incomplete, utc(2026, 1, 4), {}}));
    const auto r2 = readJournal(path2, &error);
    QVERIFY2(r2, qPrintable(error));
    QCOMPARE(r2->state(), BatchState::Incomplete);
}

void TestJournal::startRecordsOpen()
{
    TempTree t;
    const QString id = newId();
    const Plan plan = samplePlan(id, {"/a", "/b"});
    QString error;
    auto w = JournalWriter::create(t.root(), sampleHeader(id, utc(2026, 1, 1)), plan, &error);
    QVERIFY2(w, qPrintable(error));
    // Step 0: start, then its result. Step 1: start only (a crash during the rename).
    QVERIFY(w->appendStart(plan.steps[0], Phase::Forward));
    QVERIFY(w->appendStep(plan.steps[0], okRecord(0, Phase::Forward, utc(2026, 1, 1))));
    QVERIFY(w->appendStart(plan.steps[1], Phase::Forward));
    QVERIFY(w->appendStart(plan.steps[0], Phase::Rollback));

    QFile f(w->filePath());
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QList<QByteArray> lines = f.readAll().split('\n');
    const QJsonObject start = QJsonDocument::fromJson(lines[2]).object();
    QCOMPARE(start.value("type").toString(), QString("start"));
    QCOMPARE(start.value("step").toInt(), 0);
    QCOMPARE(start.value("phase").toString(), QString("forward"));
    QCOMPARE(start.value("from").toString(), plan.steps[0].from);
    QCOMPARE(start.value("to").toString(), plan.steps[0].to);
    QVERIFY(!start.value("time").toString().isEmpty());
    const QJsonObject rollback = QJsonDocument::fromJson(lines[5]).object();
    QCOMPARE(rollback.value("from").toString(), plan.steps[0].to); // the reverse rename
    QCOMPARE(rollback.value("to").toString(), plan.steps[0].from);

    const auto r = readJournal(w->filePath(), &error);
    QVERIFY2(r, qPrintable(error));
    QCOMPARE(r->steps.size(), 1); // a start record is not a result
    QCOMPARE(r->openStarts.size(), 2);
    QCOMPARE(r->openStarts[0].step, 1);
    QCOMPARE(r->openStarts[0].phase, Phase::Forward);
    QCOMPARE(r->openStarts[1].step, 0);
    QCOMPARE(r->openStarts[1].phase, Phase::Rollback);

    // A journal of the old format has no start records.
    const QString old = writeRaw(t.root(), newId(), utc(2026, 1, 2), BatchState::Complete);
    QVERIFY(readJournal(old, &error)->openStarts.isEmpty());
}

void TestJournal::openAndReadAgree_data()
{
    QTest::addColumn<QByteArray>("extra");
    QTest::addColumn<bool>("readable");
    const QByteArray op = "{\"type\":\"state\",\"state\":\"incomplete\",\"time\":\"x\"}\n";
    QTest::newRow("clean") << QByteArray() << true;
    QTest::newRow("tail") << QByteArray("{\"type\":\"op\",\"st") << true;
    QTest::newRow("bad last line") << QByteArray("{\"type\":\"op\",\"st\n") << true;
    QTest::newRow("good line, tail") << op + "{\"ty" << true;
    // A complete line before a tail is not the last line: it must be valid.
    QTest::newRow("bad line, tail") << QByteArray("not json\n{\"ty") << false;
    // Valid JSON, but not a valid record: never a torn write.
    QTest::newRow("bad record last") << QByteArray("{\"type\":\"op\",\"step\":99}\n") << false;
    QTest::newRow("bad middle") << QByteArray("not json\n") + op << false;
}

// openForAppend() accepts exactly the journals that readJournal() reads, and cuts exactly the
// tail that readJournal() ignores.
void TestJournal::openAndReadAgree()
{
    QFETCH(QByteArray, extra);
    QFETCH(bool, readable);
    TempTree t;
    const QString path = writeRaw(t.root(), newId(), utc(2026, 1, 1), BatchState::InProgress);
    appendBytes(path, extra);
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray before = f.readAll();
    f.close();

    QString error;
    const auto r = readJournal(path, &error);
    QCOMPARE(bool(r), readable);
    auto w = JournalWriter::openForAppend(path, &error);
    QCOMPARE(bool(w), readable);
    if (!readable) {
        QVERIFY(!error.isEmpty());
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), before); // the file is not changed
        return;
    }
    QVERIFY(w->appendState({BatchState::RolledBack, utc(2026, 1, 2), {}}));
    const auto after = readJournal(path, &error);
    QVERIFY2(after, qPrintable(error));
    QVERIFY(!after->ignoredTail);
    QCOMPARE(after->states.size(), r->states.size() + 1);
    QCOMPARE(after->steps.size(), r->steps.size());
    QCOMPARE(after->state(), BatchState::RolledBack);
}

void TestJournal::bestEffortOpenWithoutHeader()
{
    TempTree t;
    const QString path = t.file("x.jsonl", "garbage\nnot json\n{\"type\":\"op\",\"st");
    QString error;
    QVERIFY(!JournalWriter::openForAppend(path, &error));
    auto w = JournalWriter::openForAppend(path, &error, ReadMode::BestEffort);
    QVERIFY2(w, qPrintable(error));
    QVERIFY(w->appendState({BatchState::Complete, utc(2026, 1, 2), {}}));
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QList<QByteArray> lines = f.readAll().split('\n');
    QCOMPARE(lines.size(), 4); // garbage, not json, state, ""
    QVERIFY(lines[2].contains("\"complete\""));
    // No complete line at all: nothing to append to.
    QVERIFY(!JournalWriter::openForAppend(t.file("y.jsonl", "{\"ty"), &error,
                                          ReadMode::BestEffort));
}

void TestJournal::listNewestFirst()
{
    TempTree t;
    const QString a = newId();
    const QString b = newId();
    const QString c = newId();
    writeRaw(t.root(), a, utc(2026, 1, 1), BatchState::Complete, {}, {"/x", "/y", "/x"});
    writeRaw(t.root(), b, utc(2026, 3, 1), BatchState::RolledBack, {}, {"/z"});
    writeRaw(t.root(), c, utc(2026, 2, 1), BatchState::Complete);
    HistoryStore store(t.root());
    QCOMPARE(store.dir(), t.root());
    const auto list = store.list();
    QCOMPARE(list.size(), 3);
    QCOMPARE(list[0].batchId, b);
    QCOMPARE(list[1].batchId, c);
    QCOMPARE(list[2].batchId, a);
    QCOMPARE(list[2].itemCount, 3);
    QCOMPARE(list[2].folders, QStringList({"/x", "/y"}));
    QCOMPARE(list[0].state, BatchState::RolledBack);
    QCOMPARE(list[0].started, utc(2026, 3, 1));
    QCOMPARE(list[0].ended, utc(2026, 3, 1));
}

void TestJournal::listShowsDamaged()
{
    const QDateTime now = utc(2026, 11, 17);
    TempTree t;
    const QString good = newId();
    writeRaw(t.root(), good, now.addDays(-400), BatchState::Complete);
    // No header can be read: the ID and the time come from the file name.
    const QString bad = t.file("20260101T000000000Z-bad.jsonl", "garbage\nmore\n");
    // A bad middle line: the other lines are read on a best-effort basis.
    const QString middleId = newId();
    const QString middle = writeRaw(t.root(), middleId, now.addDays(-400), BatchState::InProgress);
    appendBytes(middle, "not json\n{\"type\":\"state\",\"state\":\"incomplete\",\"time\":\"x\"}\n");
    t.file("notes.txt", "x");

    HistoryStore store(t.root());
    const QList<BatchSummary> list = store.list();
    QCOMPARE(list.size(), 3);
    QHash<QString, BatchSummary> byId;
    for (const BatchSummary &s : list)
        byId.insert(s.batchId, s);
    QVERIFY(!byId.value(good).damaged);
    QVERIFY(byId.value(good).error.isEmpty());

    QVERIFY(byId.contains("bad"));
    const BatchSummary b = byId.value("bad");
    QVERIFY(b.damaged);
    QVERIFY(!b.error.isEmpty());
    QCOMPARE(b.filePath, bad);
    QCOMPARE(b.started, utc(2026, 1, 1, 0, 0, 0));
    QCOMPARE(b.state, BatchState::InProgress); // unknown: treated as not finished

    const BatchSummary m = byId.value(middleId);
    QVERIFY(m.damaged);
    QVERIFY(!m.error.isEmpty());
    QCOMPARE(m.state, BatchState::Incomplete);
    QCOMPARE(m.itemCount, 1);

    // A damaged journal is never deleted.
    QCOMPARE(store.applyRetention(0, 1, now), 1);
    QCOMPARE(store.clear(), 0);
    QVERIFY(QFile::exists(bad));
    QVERIFY(QFile::exists(middle));
    QCOMPARE(store.list().size(), 2);
}

void TestJournal::inProgressListed()
{
    TempTree t;
    const QString a = newId();
    writeRaw(t.root(), a, utc(2026, 1, 1), BatchState::InProgress);
    writeRaw(t.root(), newId(), utc(2026, 1, 2), BatchState::Complete);
    writeRaw(t.root(), newId(), utc(2026, 1, 3), BatchState::Incomplete);
    HistoryStore store(t.root());
    const auto list = store.inProgress();
    QCOMPARE(list.size(), 1);
    QCOMPARE(list[0].batchId, a);
}

void TestJournal::undoBatchesOf()
{
    TempTree t;
    const QString a = newId();
    const QString u1 = newId();
    const QString u2 = newId();
    writeRaw(t.root(), a, utc(2026, 1, 1), BatchState::PartlyUndone);
    writeRaw(t.root(), u2, utc(2026, 1, 3), BatchState::Complete, a);
    writeRaw(t.root(), u1, utc(2026, 1, 2), BatchState::Complete, a);
    writeRaw(t.root(), newId(), utc(2026, 1, 4), BatchState::Complete);
    HistoryStore store(t.root());
    const auto undos = store.undoBatchesOf(a);
    QCOMPARE(undos.size(), 2);
    QCOMPARE(undos[0].header.batchId, u1);
    QCOMPARE(undos[1].header.batchId, u2);
    QCOMPARE(undos[0].header.undoOf, a);
    QVERIFY(store.undoBatchesOf(u1).isEmpty());
    const auto list = store.list();
    QCOMPARE(list[1].undoOf, a);
}

void TestJournal::loadAndAppendStateById()
{
    TempTree t;
    const QString a = newId();
    writeRaw(t.root(), a, utc(2026, 1, 1), BatchState::Complete);
    HistoryStore store(t.root());
    QString error;
    const auto r = store.load(a, &error);
    QVERIFY2(r, qPrintable(error));
    QCOMPARE(r->header.batchId, a);
    QVERIFY(!store.load(newId(), &error));
    QVERIFY(!error.isEmpty());

    const QString undo = newId();
    QVERIFY2(store.appendState(a, {BatchState::PartlyUndone, utc(2026, 1, 5), undo}, &error),
             qPrintable(error));
    const auto after = store.load(a, &error);
    QCOMPARE(after->state(), BatchState::PartlyUndone);
    QCOMPARE(after->states.last().undoBatchId, undo);
    QVERIFY(!store.appendState(newId(), {BatchState::Complete, utc(2026, 1, 5), {}}, &error));
}

void TestJournal::retentionKeepsByCountOrAge()
{
    const QDateTime now = utc(2026, 11, 17);
    {
        TempTree t;
        for (int i = 0; i < 600; ++i)
            writeRaw(t.root(), newId(), now.addDays(-200).addSecs(i), BatchState::Complete);
        HistoryStore store(t.root());
        QCOMPARE(store.applyRetention(500, 180, now), 100);
        const auto list = store.list();
        QCOMPARE(list.size(), 500);
        // The newest 500 stay.
        QCOMPARE(list.last().started, now.addDays(-200).addSecs(100));
    }
    {
        TempTree t;
        for (int i = 0; i < 600; ++i)
            writeRaw(t.root(), newId(), now.addDays(-7).addSecs(i), BatchState::Complete);
        HistoryStore store(t.root());
        QCOMPARE(store.applyRetention(500, 180, now), 0);
        QCOMPARE(store.list().size(), 600);
    }
}

void TestJournal::retentionKeepsUnsafe()
{
    const QDateTime now = utc(2026, 11, 17);
    TempTree t;
    const QString inProgress = newId();
    const QString incomplete = newId();
    writeRaw(t.root(), inProgress, now.addDays(-400), BatchState::InProgress);
    writeRaw(t.root(), incomplete, now.addDays(-400), BatchState::Incomplete);
    for (const BatchState s : {BatchState::Complete, BatchState::RolledBack, BatchState::Undone,
                               BatchState::PartlyUndone})
        writeRaw(t.root(), newId(), now.addDays(-400), s);
    HistoryStore store(t.root());
    QCOMPARE(store.applyRetention(0, 180, now), 4);
    QStringList ids;
    for (const BatchSummary &s : store.list())
        ids.append(s.batchId);
    ids.sort();
    QStringList want = {inProgress, incomplete};
    want.sort();
    QCOMPARE(ids, want);
}

void TestJournal::clearKeepsUnsafe()
{
    TempTree t;
    const QDateTime now = utc(2026, 11, 17);
    writeRaw(t.root(), newId(), now, BatchState::Complete);
    writeRaw(t.root(), newId(), now, BatchState::RolledBack);
    const QString inProgress = newId();
    const QString incomplete = newId();
    writeRaw(t.root(), inProgress, now, BatchState::InProgress);
    writeRaw(t.root(), incomplete, now, BatchState::Incomplete);
    // An undo batch of an incomplete batch stays with its group.
    const QString undoOfIncomplete = newId();
    writeRaw(t.root(), undoOfIncomplete, now, BatchState::Complete, incomplete);
    HistoryStore store(t.root());
    QCOMPARE(store.clear(), 2);
    QCOMPARE(store.list().size(), 3);
}

void TestJournal::retentionDeletesGroups()
{
    const QDateTime now = utc(2026, 11, 17);
    TempTree t;
    const QString oldA = newId();
    const QString oldUndoA = newId();
    writeRaw(t.root(), oldA, now.addDays(-300), BatchState::Undone);
    writeRaw(t.root(), oldUndoA, now.addDays(-290), BatchState::Complete, oldA);
    const QString oldB = newId();
    const QString newUndoB = newId();
    writeRaw(t.root(), oldB, now.addDays(-300), BatchState::PartlyUndone);
    writeRaw(t.root(), newUndoB, now.addDays(-10), BatchState::Complete, oldB);
    HistoryStore store(t.root());
    QCOMPARE(store.applyRetention(0, 180, now), 2);
    QStringList ids;
    for (const BatchSummary &s : store.list())
        ids.append(s.batchId);
    ids.sort();
    QStringList want = {oldB, newUndoB};
    want.sort();
    QCOMPARE(ids, want);

    // An old group with one member in the newest keepCount is kept whole.
    TempTree t2;
    const QString c = newId();
    const QString undoC = newId();
    writeRaw(t2.root(), c, now.addDays(-300), BatchState::Undone);
    writeRaw(t2.root(), undoC, now.addDays(-250), BatchState::Complete, c);
    writeRaw(t2.root(), newId(), now.addDays(-260), BatchState::Complete);
    HistoryStore store2(t2.root());
    QCOMPARE(store2.applyRetention(1, 180, now), 1);
    QCOMPARE(store2.list().size(), 2);
}

void TestJournal::fileNameTime()
{
    const QDateTime withMs(QDate(2026, 11, 1), QTime(10, 0, 0, 123), QTimeZone::UTC);
    QCOMPARE(journalFileTime(QStringLiteral("20261101T100000123Z-abc.jsonl")), withMs);
    const QDateTime noMs(QDate(2026, 11, 1), QTime(10, 0), QTimeZone::UTC);
    QCOMPARE(journalFileTime(QStringLiteral("20261101T100000Z-abc.jsonl")), noMs);
    QCOMPARE(journalFileTime(QStringLiteral("/h/20261101T100000Z-abc.jsonl")), noMs);
    QCOMPARE(journalFileTime(QStringLiteral("20261101T100000Z-abc.jsonl")).timeSpec(), Qt::UTC);
    QVERIFY(!journalFileTime(QStringLiteral("abc.jsonl")).isValid());
    QVERIFY(!journalFileTime(QStringLiteral("x-abc.jsonl")).isValid());
    QVERIFY(!journalFileTime(QString()).isValid());

    // The writer's name reads back.
    TempTree tree;
    JournalHeader h;
    h.batchId = QStringLiteral("b1");
    h.started = QDateTime(QDate(2026, 11, 17), QTime(14, 2, 3, 45), QTimeZone::UTC);
    QString error;
    const auto w = JournalWriter::create(tree.root(), h, Plan{}, &error);
    QVERIFY2(w, qPrintable(error));
    QCOMPARE(journalFileTime(w->filePath()), h.started);
}

void TestJournal::damagedListTimeWithoutMs()
{
    TempTree tree;
    QFile f(tree.root() + QStringLiteral("/20261101T100000Z-dmg.jsonl"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("not json\n");
    f.close();
    const QList<BatchSummary> list = HistoryStore(tree.root()).list();
    QCOMPARE(list.size(), 1);
    QVERIFY(list[0].damaged);
    QCOMPARE(list[0].batchId, QStringLiteral("dmg"));
    QCOMPARE(list[0].started, QDateTime(QDate(2026, 11, 1), QTime(10, 0), QTimeZone::UTC));
}

QTEST_GUILESS_MAIN(TestJournal)
#include "tst_journal.moc"
