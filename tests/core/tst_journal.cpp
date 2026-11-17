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
    void notAJournalFails();
    void fileNameHasNoColon();
    void createNeverReplaces();
    void appendAfterReopen();
    void appendAfterTruncatedTail();
    void listNewestFirst();
    void listSkipsUnreadable();
    void inProgressListed();
    void undoBatchesOf();
    void loadAndAppendStateById();
    void retentionKeepsByCountOrAge();
    void retentionKeepsUnsafe();
    void clearKeepsUnsafe();
    void retentionDeletesGroups();
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

void TestJournal::listSkipsUnreadable()
{
    TempTree t;
    writeRaw(t.root(), newId(), utc(2026, 1, 1), BatchState::Complete);
    const QString bad = t.file("20260101T000000000Z-bad.jsonl", "garbage\nmore\n");
    t.file("notes.txt", "x");
    HistoryStore store(t.root());
    QCOMPARE(store.list().size(), 1);
    QCOMPARE(store.clear(), 1);
    QVERIFY(QFile::exists(bad)); // an unreadable file is never deleted
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

QTEST_GUILESS_MAIN(TestJournal)
#include "tst_journal.moc"
