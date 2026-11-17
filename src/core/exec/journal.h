// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "exec/plan.h"

#include <QDateTime>
#include <QFile>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <memory>
#include <optional>

namespace ctrm {

// Exact strings: "in-progress", "complete", "rolled-back", "incomplete", "partly-undone", "undone".
enum class BatchState { InProgress, Complete, RolledBack, Incomplete, PartlyUndone, Undone };
QString batchStateToString(BatchState s);
std::optional<BatchState> batchStateFromString(const QString &s);

enum class Phase { Forward, Rollback };

struct StepRecord
{
    int step = 0;
    Phase phase = Phase::Forward;
    bool ok = false;
    int errorCode = 0;
    QString errorText;
    QString fallback;
    QDateTime time;
};

struct StateRecord
{
    BatchState state;
    QDateTime time;
    QString undoBatchId;
};

struct JournalHeader
{
    QString batchId;
    QDateTime started;
    QString ctrmVersion;
    QString os;
    QJsonObject chain; // preset object (Task 17 format, unordered)
    QString undoOf;    // empty for a normal batch
};

struct BatchRecord
{
    JournalHeader header;
    Plan plan;
    QList<StepRecord> steps;
    QList<StateRecord> states;
    QString filePath;
    bool ignoredTail = false;
    QList<int> skippedLines;  // 1-based; bad middle lines skipped by a ReadMode::BestEffort read
    BatchState state() const; // last state record (InProgress when there is none)
    QDateTime ended() const;  // time of the last state record
};

class JournalSink
{
public:
    virtual ~JournalSink() = default;
    // Forward: the record has from = step.from, to = step.to.
    // Rollback: the record has from = step.to, to = step.from (the reverse rename).
    virtual bool appendStep(const PlanStep &step, const StepRecord &rec) = 0;
    virtual bool appendState(const StateRecord &rec) = 0;
};

// One .jsonl file per batch. Each line is written whole and synced to the disk (fsync)
// before the call returns. When a write or the sync fails, the part of the line is removed
// again, so that the next record starts on its own line.
class JournalWriter : public JournalSink
{
public:
    // Makes "<historyDir>/yyyyMMddTHHmmsszzzZ-<batchId>.jsonl" (never replaces a file), writes
    // the header and the in-progress state. nullptr and *error set on failure.
    static std::unique_ptr<JournalWriter> create(const QString &historyDir,
                                                 const JournalHeader &header, const Plan &plan,
                                                 QString *error);
    // Opens an existing journal for more records. An incomplete last line (a crash during a
    // write) is cut off first, so that the next record starts on a new line.
    static std::unique_ptr<JournalWriter> openForAppend(const QString &filePath, QString *error);

    bool appendStep(const PlanStep &step, const StepRecord &rec) override;
    bool appendState(const StateRecord &rec) override;
    QString filePath() const;

private:
    explicit JournalWriter(const QString &filePath);
    bool writeLine(const QJsonObject &o);

    QFile m_file;
};

// Strict: every complete line must be valid. BestEffort (summaries only, never a roll back):
// a bad line other than the header is skipped; its number goes into skippedLines.
enum class ReadMode { Strict, BestEffort };

// nullopt and *error set when the file cannot be read, the first line is not a header, or
// (Strict) a line other than the last one is not a valid record. An incomplete last line is
// ignored (ignoredTail = true).
std::optional<BatchRecord> readJournal(const QString &filePath, QString *error,
                                       ReadMode mode = ReadMode::Strict);

} // namespace ctrm
