// SPDX-License-Identifier: GPL-3.0-or-later
#include "preview/previewengine.h"

#include "model/nameparts.h"

#include <utility>

namespace ctrm {

namespace {

constexpr int kCancelInterval = 256;

bool cancelled(const std::atomic<bool> *cancel, int i)
{
    return cancel && i % kCancelInterval == 0 && cancel->load(std::memory_order_relaxed);
}

// The key of a name when case and normalization are both ignored.
QString caseOnlyKey(const QString &name)
{
    static const VolumeInfo loose{CaseSensitivity::Insensitive, true, IdentityType::Weak, {}};
    return nameKey(name, loose);
}

QString nfd(const QString &s)
{
    return s.normalized(QString::NormalizationForm_D);
}

bool isExistsCode(DiagCode c)
{
    return c == DiagCode::ExistsInFolder || c == DiagCode::ExistsIgnoringCase ||
           c == DiagCode::ExistsNormalization;
}

// Gives a new name its status (Unchanged, Invalid, Changed or CaseOnly) and its warnings.
void classify(ItemPreview &p, const Item &item, const QString &newName,
              const NameValidator &validator, bool folderExtensions)
{
    p.newName = newName;
    p.problems.clear();
    p.warnings.clear();
    if (newName == item.name) {
        p.status = Status::Unchanged;
        return;
    }
    std::optional<Diagnostic> invalid = validator.checkName(newName);
    if (!invalid)
        invalid = validator.checkPath(item.folder, newName);
    if (invalid) {
        p.status = Status::Invalid;
        p.problems.append(*invalid);
        return;
    }
    p.status = caseOnlyKey(newName) == caseOnlyKey(item.name) ? Status::CaseOnly : Status::Changed;

    p.warnings = validator.nameWarnings(newName);
    p.warnings += validator.pathWarnings(item.folder, newName);
    const bool wasHidden = item.name.startsWith(QLatin1Char('.'));
    const bool isHidden = newName.startsWith(QLatin1Char('.'));
    if (isHidden && !wasHidden)
        p.warnings.append({DiagCode::BecomesHidden, {}});
    else if (wasHidden && !isHidden)
        p.warnings.append({DiagCode::BecomesVisible, {}});
    if (!item.isFolder) {
        const QString oldExt = splitName(item.name, false, folderExtensions).extension;
        const QString newExt = splitName(newName, false, folderExtensions).extension;
        if (oldExt.toCaseFolded() != newExt.toCaseFolded())
            p.warnings.append({DiagCode::ExtensionChanged, {}});
    }
}

QString numberedName(const QString &name, int n, bool isFolder, bool folderExtensions)
{
    NameParts parts = splitName(name, isFolder, folderExtensions);
    parts.stem += QStringLiteral(" (%1)").arg(n);
    return joinName(parts);
}

// Exact names on disk per key. A key can hold more than one exact name when the volume
// probe was wrong (Unknown volume that is really case-sensitive).
using Occupied = QHash<QString, QStringList>;

void addOccupied(Occupied &occ, const QString &key, const QString &exact)
{
    occ[key].append(exact);
}

void removeOccupied(Occupied &occ, const QString &key, const QString &exact)
{
    auto it = occ.find(key);
    if (it == occ.end())
        return;
    it->removeOne(exact);
    if (it->isEmpty())
        occ.erase(it);
}

// The conflict diagnostic for newName against the exact names on disk with the same key.
Diagnostic existsDiagnostic(const QString &newName, const QStringList &existing)
{
    if (existing.contains(newName))
        return {DiagCode::ExistsInFolder, newName};
    const QString newNfd = nfd(newName);
    for (const QString &e : existing) {
        if (nfd(e) == newNfd)
            return {DiagCode::ExistsNormalization, e};
    }
    return {DiagCode::ExistsIgnoringCase, existing.first()};
}

struct Candidate
{
    int index;
    QString oldKey;
    QString newKey;
};

// Steps 3 and 4 of the algorithm for the items of one folder.
void resolveFolder(QList<ItemPreview> &out, const PreviewInput &in, const FolderInfo &folder,
                   const QList<int> &itemIndexes, const NameValidator &validator)
{
    const VolumeInfo &vol = folder.volume;
    const bool folderExt = in.chain.options.folderExtensions;

    Occupied occupied;
    occupied.reserve(folder.names.size());
    for (const QString &n : folder.names)
        addOccupied(occupied, nameKey(n, vol), n);

    QList<Candidate> cands;
    for (int i : itemIndexes) {
        const Status s = out[i].status;
        if (s != Status::Changed && s != Status::CaseOnly)
            continue;
        const Item &item = in.items[i];
        const QString oldKey = nameKey(item.name, vol);
        removeOccupied(occupied, oldKey, item.name);
        cands.append({i, oldKey, nameKey(out[i].newName, vol)});
    }

    if (in.chain.options.autoResolveDuplicates) {
        QHash<QString, QList<int>> groups; // new key -> candidate positions, table order
        QSet<QString> taken;
        for (int c = 0; c < cands.size(); ++c) {
            groups[cands[c].newKey].append(c);
            taken.insert(cands[c].newKey);
        }
        // Walk in table order so that the result does not depend on hash order.
        QSet<QString> done;
        for (int c = 0; c < cands.size(); ++c) {
            const QString key = cands[c].newKey;
            if (done.contains(key))
                continue;
            done.insert(key);
            const QList<int> &group = groups[key];
            const bool keyFree = !occupied.contains(key);
            if (group.size() == 1 && keyFree)
                continue;
            int n = 2;
            for (int g = keyFree ? 1 : 0; g < group.size(); ++g) {
                Candidate &cand = cands[group[g]];
                const Item &item = in.items[cand.index];
                const QString base = out[cand.index].newName;
                QString name;
                QString nameK;
                for (;; ++n) {
                    name = numberedName(base, n, item.isFolder, folderExt);
                    nameK = nameKey(name, vol);
                    if (!occupied.contains(nameK) && !taken.contains(nameK))
                        break;
                }
                ++n;
                taken.insert(nameK);
                cand.newKey = nameK;
                classify(out[cand.index], item, name, validator, folderExt);
            }
        }
        // Items that are no longer candidates keep their old names.
        QList<Candidate> kept;
        for (const Candidate &cand : std::as_const(cands)) {
            const Status s = out[cand.index].status;
            if (s == Status::Changed || s == Status::CaseOnly)
                kept.append(cand);
            else
                addOccupied(occupied, cand.oldKey, in.items[cand.index].name);
        }
        cands = kept;
    }

    // First pass: all conflicts of the first round at the same time.
    QHash<QString, int> newKeyCount;
    for (const Candidate &cand : std::as_const(cands))
        ++newKeyCount[cand.newKey];
    QHash<QString, QList<int>> byNewKey; // active candidates (positions in cands)
    QList<int> returned;                 // candidates that became Conflict
    for (int c = 0; c < cands.size(); ++c) {
        const Candidate &cand = cands[c];
        ItemPreview &p = out[cand.index];
        const int same = newKeyCount.value(cand.newKey);
        if (same > 1) {
            Diagnostic d{DiagCode::DuplicateInBatch, {}};
            d.count = same - 1;
            p.status = Status::Conflict;
            p.problems = {d};
            returned.append(c);
        } else if (auto it = occupied.constFind(cand.newKey); it != occupied.cend()) {
            p.status = Status::Conflict;
            p.problems = {existsDiagnostic(p.newName, *it)};
            returned.append(c);
        } else {
            byNewKey[cand.newKey].append(c);
        }
    }
    // Cascade: a candidate that became Conflict keeps its old name, which can block another.
    for (int r = 0; r < returned.size(); ++r) {
        const Candidate &back = cands[returned[r]];
        addOccupied(occupied, back.oldKey, in.items[back.index].name);
        const QList<int> hit = byNewKey.take(back.oldKey);
        for (int c : hit) {
            ItemPreview &p = out[cands[c].index];
            p.status = Status::Conflict;
            p.problems = {existsDiagnostic(p.newName, occupied.value(back.oldKey))};
            returned.append(c);
        }
    }
}

PreviewSummary summarize(const QList<ItemPreview> &items)
{
    PreviewSummary s;
    for (const ItemPreview &p : items) {
        if (!p.included) {
            ++s.excluded;
            continue;
        }
        if (p.skipped) {
            ++s.skipped;
            continue;
        }
        switch (p.status) {
        case Status::Unchanged:
            ++s.unchanged;
            break;
        case Status::Changed:
        case Status::CaseOnly:
            ++s.willChange;
            break;
        case Status::Conflict:
            ++s.conflicts;
            break;
        case Status::Invalid:
        case Status::Missing:
        case Status::RuleError:
            ++s.errors;
            break;
        }
        if (!isBlocking(p.status) && !p.warnings.isEmpty())
            ++s.warnings;
    }
    return s;
}

// The rule chain output for each item. Empty optional when cancelled.
std::optional<QList<ProposedName>> proposeNames(const PreviewInput &in,
                                                const std::atomic<bool> *cancel)
{
    QList<ProposedName> names;
    names.reserve(in.items.size());

    if (const auto err = in.chain.firstError()) {
        Diagnostic d{err->problem.unsupported ? DiagCode::UnsupportedRule : DiagCode::RuleFailed,
                     err->problem.unsupported ? in.chain.at(err->ruleIndex)->type()
                                              : err->problem.message};
        d.ruleIndex = err->ruleIndex;
        if (!err->problem.unsupported)
            d.count = err->problem.position;
        for (const Item &item : in.items)
            names.append({item.name, d});
        return names;
    }

    int includedCount = 0;
    for (const Item &item : in.items) {
        if (item.included)
            ++includedCount;
    }
    RuleContext ctx;
    ctx.count = int(in.items.size());
    ctx.includedCount = includedCount;
    ctx.folderExtensions = in.chain.options.folderExtensions;
    ctx.portableNames = in.chain.options.portableNames;
    ctx.os = in.os;
    int includedIndex = 0;
    for (int i = 0; i < in.items.size(); ++i) {
        if (cancelled(cancel, i))
            return std::nullopt;
        const Item &item = in.items[i];
        if (!item.included) {
            names.append({item.name, std::nullopt});
            continue;
        }
        ctx.index = i;
        ctx.includedIndex = includedIndex++; // a skipped item keeps its number (PM D10)
        ctx.isFolder = item.isFolder;
        names.append({in.chain.apply(item.name, ctx), std::nullopt});
    }
    return names;
}

} // namespace

PreviewResult evaluateNames(const PreviewInput &in, const QList<ProposedName> &names,
                            const NameValidator &validator, const std::atomic<bool> *cancel)
{
    PreviewResult result;
    const int n = int(in.items.size());
    Q_ASSERT(names.size() == n);
    result.items.resize(n);
    const bool folderExt = in.chain.options.folderExtensions;

    QHash<QString, QSet<QString>> namesOnDisk; // folder -> exact names
    QHash<QString, QList<int>> byFolder;       // folder -> items that can be candidates

    for (int i = 0; i < n; ++i) {
        if (cancelled(cancel, i)) {
            result.cancelled = true;
            return result;
        }
        const Item &item = in.items[i];
        ItemPreview &p = result.items[i];
        p.newName = item.name;
        p.included = item.included;
        if (!item.included)
            continue;
        if (in.skip.contains(item.id)) {
            p.skipped = true;
            continue;
        }
        const auto folderIt = in.folders.constFind(item.folder);
        bool onDisk = false;
        if (folderIt != in.folders.cend() && folderIt->readable) {
            auto setIt = namesOnDisk.find(item.folder);
            if (setIt == namesOnDisk.end())
                setIt = namesOnDisk.insert(item.folder, QSet<QString>(folderIt->names.cbegin(),
                                                                      folderIt->names.cend()));
            onDisk = setIt->contains(item.name);
        }
        if (!onDisk) {
            p.status = Status::Missing;
            p.problems = {{DiagCode::NotOnDisk, {}}};
            continue;
        }
        if (in.identityChanged.contains(item.id)) {
            p.status = Status::Missing;
            p.problems = {{DiagCode::IdentityChanged, {}}};
            continue;
        }
        if (names[i].ruleError) {
            p.status = Status::RuleError;
            p.problems = {*names[i].ruleError};
            continue;
        }
        classify(p, item, names[i].newName, validator, folderExt);
        byFolder[item.folder].append(i);
    }

    for (auto it = byFolder.cbegin(); it != byFolder.cend(); ++it) {
        if (cancel && cancel->load(std::memory_order_relaxed)) {
            result.cancelled = true;
            return result;
        }
        resolveFolder(result.items, in, in.folders[it.key()], it.value(), validator);
    }

    result.summary = summarize(result.items);
    return result;
}

PreviewResult computePreview(const PreviewInput &in, const std::atomic<bool> *cancel)
{
    const std::optional<QList<ProposedName>> names = proposeNames(in, cancel);
    if (!names) {
        PreviewResult r;
        r.cancelled = true;
        return r;
    }
    return evaluateNames(in, *names, NameValidator(in.os, in.chain.options.portableNames), cancel);
}

SkipResult previewSkippingBlocked(const PreviewInput &in)
{
    SkipResult out;
    const QList<ProposedName> names = *proposeNames(in, nullptr);
    const NameValidator validator(in.os, in.chain.options.portableNames);
    const PreviewResult first = evaluateNames(in, names, validator);

    // Old-name keys of the blocked items, per folder, with the number of items per key.
    QHash<QString, QHash<QString, int>> blockedKeys;
    for (int i = 0; i < in.items.size(); ++i) {
        const ItemPreview &p = first.items[i];
        if (!p.included || p.skipped || !isBlocking(p.status))
            continue;
        const Item &item = in.items[i];
        out.skipped.insert(item.id);
        const auto f = in.folders.constFind(item.folder);
        if (f != in.folders.cend())
            ++blockedKeys[item.folder][nameKey(item.name, f->volume)];
    }

    for (int i = 0; i < in.items.size(); ++i) {
        const Item &item = in.items[i];
        const ItemPreview &p = first.items[i];
        if (!out.skipped.contains(item.id) || p.status != Status::Conflict || p.problems.isEmpty())
            continue;
        const VolumeInfo vol = in.folders.value(item.folder).volume;
        const QHash<QString, int> keys = blockedKeys.value(item.folder);
        const QString ownKey = nameKey(item.name, vol);
        const bool cascade =
                std::all_of(p.problems.cbegin(), p.problems.cend(), [&](const Diagnostic &d) {
                    if (!isExistsCode(d.code))
                        return false;
                    const QString k = nameKey(d.text, vol);
                    // Another blocked item, not this item itself.
                    return keys.value(k) > (k == ownKey ? 1 : 0);
                });
        if (cascade)
            out.cascaded.insert(item.id);
    }

    // The skipped items keep their old names. Rule output of the other items stays the same.
    PreviewInput skipping = in;
    skipping.skip.unite(out.skipped);
    out.preview = evaluateNames(skipping, names, validator);
    return out;
}

} // namespace ctrm
