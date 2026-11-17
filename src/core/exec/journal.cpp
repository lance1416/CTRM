// SPDX-License-Identifier: GPL-3.0-or-later
#include "exec/journal.h"

#include "fs/filesync.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QTimeZone>

namespace ctrm {

namespace {

const QString kFormat = QStringLiteral("ctrm-journal");
constexpr int kFormatVersion = 1;

struct StateName
{
    BatchState state;
    const char16_t *name;
};
constexpr StateName kStates[] = {
        {BatchState::InProgress, u"in-progress"},     {BatchState::Complete, u"complete"},
        {BatchState::RolledBack, u"rolled-back"},     {BatchState::Incomplete, u"incomplete"},
        {BatchState::PartlyUndone, u"partly-undone"}, {BatchState::Undone, u"undone"},
};

QString timeToString(const QDateTime &t)
{
    return t.toUTC().toString(Qt::ISODateWithMs);
}

QDateTime timeFromString(const QString &s)
{
    return QDateTime::fromString(s, Qt::ISODateWithMs).toUTC();
}

QString kindToString(StepKind k)
{
    switch (k) {
    case StepKind::Direct:
        return QStringLiteral("direct");
    case StepKind::ToTemp:
        return QStringLiteral("to-temp");
    case StepKind::FromTemp:
        return QStringLiteral("from-temp");
    }
    return {};
}

std::optional<StepKind> kindFromString(const QString &s)
{
    if (s == u"direct")
        return StepKind::Direct;
    if (s == u"to-temp")
        return StepKind::ToTemp;
    if (s == u"from-temp")
        return StepKind::FromTemp;
    return std::nullopt;
}

std::optional<ItemId> idFromJson(const QJsonValue &v)
{
    bool ok = false;
    const ItemId id = v.toString().toULongLong(&ok);
    return ok ? std::optional<ItemId>(id) : std::nullopt;
}

QJsonObject headerToJson(const JournalHeader &h, const Plan &plan)
{
    QJsonArray requests;
    for (const RenameRequest &r : plan.requests) {
        requests.append(QJsonObject{
                {QStringLiteral("item"), QString::number(r.item)},
                {QStringLiteral("folder"), r.folder},
                {QStringLiteral("oldName"), r.oldName},
                {QStringLiteral("newName"), r.newName},
                {QStringLiteral("isFolder"), r.isFolder},
                {QStringLiteral("identity"), identityToJson(r.identity)},
        });
    }
    QJsonArray steps;
    for (const PlanStep &s : plan.steps) {
        steps.append(QJsonObject{
                {QStringLiteral("step"), s.index},
                {QStringLiteral("item"), QString::number(s.item)},
                {QStringLiteral("from"), s.from},
                {QStringLiteral("to"), s.to},
                {QStringLiteral("kind"), kindToString(s.kind)},
        });
    }
    return QJsonObject{
            {QStringLiteral("type"), QStringLiteral("header")},
            {QStringLiteral("format"), kFormat},
            {QStringLiteral("formatVersion"), kFormatVersion},
            {QStringLiteral("batchId"), h.batchId},
            {QStringLiteral("started"), timeToString(h.started)},
            {QStringLiteral("ctrmVersion"), h.ctrmVersion},
            {QStringLiteral("os"), h.os},
            {QStringLiteral("chain"), h.chain},
            {QStringLiteral("undoOf"), h.undoOf.isEmpty() ? QJsonValue() : QJsonValue(h.undoOf)},
            {QStringLiteral("requests"), requests},
            {QStringLiteral("steps"), steps},
    };
}

QJsonObject stateToJson(const StateRecord &rec)
{
    QJsonObject o{
            {QStringLiteral("type"), QStringLiteral("state")},
            {QStringLiteral("state"), batchStateToString(rec.state)},
            {QStringLiteral("time"), timeToString(rec.time)},
    };
    if (!rec.undoBatchId.isEmpty())
        o.insert(QStringLiteral("undoBatch"), rec.undoBatchId);
    return o;
}

// Fills record.header and record.plan. Returns an error text, or empty.
QString parseHeader(const QJsonObject &o, BatchRecord &record)
{
    if (o.value(u"type").toString() != u"header" || o.value(u"format").toString() != kFormat)
        return QStringLiteral("The first line is not a CTRM journal header");
    if (o.value(u"formatVersion").toInt() != kFormatVersion)
        return QStringLiteral("Unsupported journal format version");
    JournalHeader &h = record.header;
    h.batchId = o.value(u"batchId").toString();
    h.started = timeFromString(o.value(u"started").toString());
    h.ctrmVersion = o.value(u"ctrmVersion").toString();
    h.os = o.value(u"os").toString();
    h.chain = o.value(u"chain").toObject();
    h.undoOf = o.value(u"undoOf").toString();
    if (h.batchId.isEmpty() || !h.started.isValid())
        return QStringLiteral("The journal header has no batch ID or start time");

    record.plan.batchId = h.batchId;
    for (const QJsonValue &v : o.value(u"requests").toArray()) {
        const QJsonObject r = v.toObject();
        const auto item = idFromJson(r.value(u"item"));
        const auto identity = identityFromJson(r.value(u"identity").toObject());
        if (!item || !identity)
            return QStringLiteral("A request in the journal header is not valid");
        RenameRequest req;
        req.item = *item;
        req.folder = r.value(u"folder").toString();
        req.oldName = r.value(u"oldName").toString();
        req.newName = r.value(u"newName").toString();
        req.isFolder = r.value(u"isFolder").toBool();
        req.identity = *identity;
        record.plan.requests.append(req);
    }
    for (const QJsonValue &v : o.value(u"steps").toArray()) {
        const QJsonObject s = v.toObject();
        const auto item = idFromJson(s.value(u"item"));
        const auto kind = kindFromString(s.value(u"kind").toString());
        if (!item || !kind || s.value(u"step").toInt(-1) != record.plan.steps.size())
            return QStringLiteral("A step in the journal header is not valid");
        PlanStep step;
        step.index = s.value(u"step").toInt();
        step.item = *item;
        step.from = s.value(u"from").toString();
        step.to = s.value(u"to").toString();
        step.kind = *kind;
        record.plan.steps.append(step);
    }
    return {};
}

// Adds an op or state record. Returns an error text, or empty. Other types are ignored.
QString parseRecord(const QJsonObject &o, BatchRecord &record)
{
    const QString type = o.value(u"type").toString();
    if (type == u"state") {
        const auto state = batchStateFromString(o.value(u"state").toString());
        if (!state)
            return QStringLiteral("Unknown batch state");
        record.states.append(StateRecord{*state, timeFromString(o.value(u"time").toString()),
                                         o.value(u"undoBatch").toString()});
        return {};
    }
    if (type == u"op") {
        StepRecord rec;
        rec.step = o.value(u"step").toInt(-1);
        if (rec.step < 0 || rec.step >= record.plan.steps.size())
            return QStringLiteral("An operation record has no valid step");
        const QString phase = o.value(u"phase").toString();
        if (phase == u"forward")
            rec.phase = Phase::Forward;
        else if (phase == u"rollback")
            rec.phase = Phase::Rollback;
        else
            return QStringLiteral("An operation record has no valid phase");
        rec.ok = o.value(u"result").toString() == u"ok";
        rec.errorCode = o.value(u"errorCode").toInt();
        rec.errorText = o.value(u"errorText").toString();
        rec.fallback = o.value(u"fallback").toString();
        rec.time = timeFromString(o.value(u"time").toString());
        record.steps.append(rec);
        return {};
    }
    return {};
}

} // namespace

QString batchStateToString(BatchState s)
{
    for (const StateName &n : kStates) {
        if (n.state == s)
            return QString::fromUtf16(n.name);
    }
    return {};
}

std::optional<BatchState> batchStateFromString(const QString &s)
{
    for (const StateName &n : kStates) {
        if (s == QStringView(n.name))
            return n.state;
    }
    return std::nullopt;
}

BatchState BatchRecord::state() const
{
    return states.isEmpty() ? BatchState::InProgress : states.last().state;
}

QDateTime BatchRecord::ended() const
{
    return states.isEmpty() ? QDateTime() : states.last().time;
}

JournalWriter::JournalWriter(const QString &filePath)
    : m_file(filePath)
{}

std::unique_ptr<JournalWriter> JournalWriter::create(const QString &historyDir,
                                                     const JournalHeader &header, const Plan &plan,
                                                     QString *error)
{
    const auto fail = [error](const QString &text) -> std::unique_ptr<JournalWriter> {
        if (error)
            *error = text;
        return nullptr;
    };
    if (!QDir().mkpath(historyDir))
        return fail(QStringLiteral("Cannot make the history folder %1").arg(historyDir));

    const QString name =
            header.started.toUTC().toString(QStringLiteral("yyyyMMdd'T'HHmmsszzz'Z'")) + u'-' +
            header.batchId + QStringLiteral(".jsonl");
    std::unique_ptr<JournalWriter> w(new JournalWriter(QDir(historyDir).filePath(name)));
    if (!w->m_file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return fail(QStringLiteral("Cannot make the journal %1: %2")
                            .arg(w->filePath(), w->m_file.errorString()));
    // On a failure after the file exists, remove it: no rename has run for this batch.
    const auto failAndRemove = [&](const QString &text) {
        w->m_file.remove();
        return fail(text);
    };
    if (!w->writeLine(headerToJson(header, plan)) ||
        !w->appendState(StateRecord{BatchState::InProgress, header.started, {}}))
        return failAndRemove(QStringLiteral("Cannot write the journal %1: %2")
                                     .arg(w->filePath(), w->m_file.errorString()));
    if (!syncFolder(historyDir))
        return failAndRemove(QStringLiteral("Cannot sync the history folder %1").arg(historyDir));
    return w;
}

std::unique_ptr<JournalWriter> JournalWriter::openForAppend(const QString &filePath, QString *error)
{
    const auto fail = [error](const QString &text) -> std::unique_ptr<JournalWriter> {
        if (error)
            *error = text;
        return nullptr;
    };
    std::unique_ptr<JournalWriter> w(new JournalWriter(filePath));
    if (!w->m_file.open(QIODevice::ReadWrite | QIODevice::ExistingOnly))
        return fail(QStringLiteral("Cannot open the journal %1: %2")
                            .arg(filePath, w->m_file.errorString()));
    // Cut off an incomplete last line, so that the next record starts on its own line.
    // readJournal() ignores the same tail: a last line with no '\n', or a last line (not the
    // header) that is not valid JSON.
    const QByteArray data = w->m_file.readAll();
    qsizetype end = data.lastIndexOf('\n') + 1;
    if (end == 0)
        return fail(QStringLiteral("The journal %1 has no complete line").arg(filePath));
    // Like readJournal(): a complete last line is checked only when no unfinished tail follows.
    const bool hasTail = end < data.size();
    const qsizetype lastStart = end >= 2 ? data.lastIndexOf('\n', end - 2) + 1 : 0;
    if (!hasTail && lastStart > 0) {
        QJsonParseError parseError;
        const QJsonDocument doc =
                QJsonDocument::fromJson(data.mid(lastStart, end - 1 - lastStart), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
            end = lastStart;
    }
    if (end < data.size() && (!w->m_file.resize(end) || !fullSync(w->m_file)))
        return fail(QStringLiteral("Cannot repair the journal %1: %2")
                            .arg(filePath, w->m_file.errorString()));
    if (!w->m_file.seek(end))
        return fail(QStringLiteral("Cannot open the journal %1: %2")
                            .arg(filePath, w->m_file.errorString()));
    return w;
}

bool JournalWriter::writeLine(const QJsonObject &o)
{
    const QByteArray line = QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n';
    if (m_file.write(line) != line.size())
        return false;
    return fullSync(m_file);
}

bool JournalWriter::appendStep(const PlanStep &step, const StepRecord &rec)
{
    const bool forward = rec.phase == Phase::Forward;
    QJsonObject o{
            {QStringLiteral("type"), QStringLiteral("op")},
            {QStringLiteral("step"), rec.step},
            {QStringLiteral("phase"),
             forward ? QStringLiteral("forward") : QStringLiteral("rollback")},
            {QStringLiteral("from"), forward ? step.from : step.to},
            {QStringLiteral("to"), forward ? step.to : step.from},
            {QStringLiteral("result"), rec.ok ? QStringLiteral("ok") : QStringLiteral("error")},
            {QStringLiteral("time"), timeToString(rec.time)},
    };
    if (!rec.ok) {
        o.insert(QStringLiteral("errorCode"), rec.errorCode);
        o.insert(QStringLiteral("errorText"), rec.errorText);
    }
    if (!rec.fallback.isEmpty())
        o.insert(QStringLiteral("fallback"), rec.fallback);
    return writeLine(o);
}

bool JournalWriter::appendState(const StateRecord &rec)
{
    return writeLine(stateToJson(rec));
}

QString JournalWriter::filePath() const
{
    return m_file.fileName();
}

std::optional<BatchRecord> readJournal(const QString &filePath, QString *error)
{
    const auto fail = [error, &filePath](const QString &text) -> std::optional<BatchRecord> {
        if (error)
            *error = QStringLiteral("%1: %2").arg(filePath, text);
        return std::nullopt;
    };
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return fail(file.errorString());
    const QByteArray data = file.readAll();

    QList<QByteArray> lines = data.split('\n');
    BatchRecord record;
    record.filePath = filePath;
    // split() gives an empty last part when the data ends with '\n'. Any other last part is a
    // line with no '\n' at the end: a write that did not finish.
    const QByteArray tail = lines.takeLast();
    if (!tail.isEmpty())
        record.ignoredTail = true;

    for (qsizetype i = 0; i < lines.size(); ++i) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(lines[i], &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            if (i == lines.size() - 1 && !record.ignoredTail && i > 0) {
                record.ignoredTail = true; // a last line that is not valid JSON
                break;
            }
            return fail(QStringLiteral("line %1 is not valid JSON").arg(i + 1));
        }
        const QString text =
                i == 0 ? parseHeader(doc.object(), record) : parseRecord(doc.object(), record);
        if (!text.isEmpty())
            return fail(QStringLiteral("line %1: %2").arg(i + 1).arg(text));
    }
    if (lines.isEmpty())
        return fail(QStringLiteral("no complete header line"));
    return record;
}

} // namespace ctrm
