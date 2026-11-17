// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model/status.h"
#include "validate/osrules.h"

#include <QList>
#include <QString>

#include <optional>

namespace ctrm {

// The rules of the OS that CTRM runs on.
OsRules currentOsRules();

// Checks new names with the rules of PRD 7.3. With portable names on, the Windows rules
// are added to the rules of the given OS.
class NameValidator
{
public:
    NameValidator(OsRules os, bool portableNames);

    // The first Invalid reason in the order of the PRD 7.3 table, or nullopt.
    std::optional<Diagnostic> checkName(const QString &name) const;
    // Warnings that do not block (never includes a check that checkName reports).
    QList<Diagnostic> nameWarnings(const QString &name) const;
    // Path length check of folder + "/" + name. Does not check the name itself.
    std::optional<Diagnostic> checkPath(const QString &folder, const QString &name) const;
    QList<Diagnostic> pathWarnings(const QString &folder, const QString &name) const;

    bool isForbidden(char32_t c) const;
    // Replaces each forbidden code point with the replacement text.
    QString replaceForbidden(const QString &name, const QString &replacement) const;

private:
    bool windowsRules() const;

    OsRules m_os;
    bool m_portable;
};

} // namespace ctrm
