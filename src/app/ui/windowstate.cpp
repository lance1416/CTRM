// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/windowstate.h"

#include "text/plural.h"

#include <QCoreApplication>
#include <QStringList>

namespace ctrm::app {

namespace {

QString renameText(const WindowStateInput &in)
{
    const int n = in.summary.willChange;
    if (n <= 0)
        return QCoreApplication::translate("ctrm::app::WindowState", "Rename");
    if (in.allFolders)
        return numerus(QCoreApplication::translate("ctrm::app::WindowState", "Rename %n Folder(s)",
                                                   nullptr, n),
                       n);
    if (in.allFiles)
        return numerus(QCoreApplication::translate("ctrm::app::WindowState", "Rename %n File(s)",
                                                   nullptr, n),
                       n);
    return numerus(
            QCoreApplication::translate("ctrm::app::WindowState", "Rename %n Item(s)", nullptr, n),
            n);
}

QString blockedReason(const ctrm::PreviewSummary &s)
{
    QStringList parts;
    if (s.conflicts > 0)
        parts.append(summaryPartText(SummaryPart::Conflicts, s.conflicts));
    if (s.errors > 0)
        parts.append(summaryPartText(SummaryPart::Errors, s.errors));
    const QString joined =
            parts.join(QCoreApplication::translate("ctrm::app::WindowState", " and "));
    // The verb agrees with the subject: one part with a count of 1 is singular.
    if (parts.size() == 1 && s.blocked() == 1)
        return QCoreApplication::translate("ctrm::app::WindowState", "%1 blocks the rename.")
                .arg(joined);
    return QCoreApplication::translate("ctrm::app::WindowState", "%1 block the rename.")
            .arg(joined);
}

} // namespace

QString summaryPartText(SummaryPart part, int n)
{
    switch (part) {
    case SummaryPart::WillChange:
        return numerus(
                QCoreApplication::translate("ctrm::app::WindowState", "%n will change", nullptr, n),
                n);
    case SummaryPart::Conflicts:
        return numerus(
                QCoreApplication::translate("ctrm::app::WindowState", "%n conflict(s)", nullptr, n),
                n);
    case SummaryPart::Errors:
        return numerus(
                QCoreApplication::translate("ctrm::app::WindowState", "%n error(s)", nullptr, n),
                n);
    case SummaryPart::Warnings:
        return numerus(
                QCoreApplication::translate("ctrm::app::WindowState", "%n warning(s)", nullptr, n),
                n);
    case SummaryPart::Excluded:
        return numerus(
                QCoreApplication::translate("ctrm::app::WindowState", "%n excluded", nullptr, n),
                n);
    case SummaryPart::Unchanged:
        return numerus(
                QCoreApplication::translate("ctrm::app::WindowState", "%n unchanged", nullptr, n),
                n);
    }
    return {};
}

QString countSummaryText(const ctrm::PreviewSummary &s)
{
    const std::pair<SummaryPart, int> parts[] = {
            {SummaryPart::WillChange, s.willChange}, {SummaryPart::Conflicts, s.conflicts},
            {SummaryPart::Errors, s.errors},         {SummaryPart::Warnings, s.warnings},
            {SummaryPart::Excluded, s.excluded},     {SummaryPart::Unchanged, s.unchanged},
    };
    QStringList texts;
    for (const auto &[part, n] : parts) {
        if (n > 0)
            texts.append(summaryPartText(part, n));
    }
    if (texts.isEmpty())
        return QCoreApplication::translate("ctrm::app::WindowState", "No items");
    return texts.join(QStringLiteral(" · "));
}

WindowStateInfo computeWindowState(const WindowStateInput &in)
{
    const ctrm::PreviewSummary &s = in.summary;
    WindowStateInfo info;
    info.renameText = renameText(in);
    info.primary = PrimaryTarget::None;

    if (in.running) {
        info.state = WindowState::Running;
    } else if (in.resultShown) {
        info.state = WindowState::Result;
        info.reason = QCoreApplication::translate("ctrm::app::WindowState", "Nothing to rename.");
    } else if (in.itemCount <= 0) {
        info.state = WindowState::NoItems;
        info.primary = PrimaryTarget::AddFiles;
        info.reason = QCoreApplication::translate("ctrm::app::WindowState", "Add files to start.");
    } else if (in.enabledRuleCount <= 0) {
        info.state = WindowState::NoRules;
        info.primary = PrimaryTarget::AddRule;
        info.reason = QCoreApplication::translate("ctrm::app::WindowState",
                                                  "Add a rule to change the names.");
    } else if (s.blocked() > 0) {
        info.state = WindowState::Blocked;
        info.reason = blockedReason(s);
        info.skipVisible = s.willChange > 0;
        if (info.skipVisible)
            info.skipText = numerus(QCoreApplication::translate("ctrm::app::WindowState",
                                                                "Skip %n and Rename...", nullptr,
                                                                s.blocked()),
                                    s.blocked());
    } else if (s.willChange <= 0) {
        info.state = WindowState::NoChange;
        info.reason = QCoreApplication::translate("ctrm::app::WindowState",
                                                  "No name changes. Edit the rules.");
    } else {
        info.state = WindowState::Ready;
        info.primary = PrimaryTarget::Rename;
        info.renameEnabled = true;
    }

    // ui-design 5.3.
    info.stepDone = {in.itemCount > 0, in.enabledRuleCount > 0,
                     in.itemCount > 0 && s.blocked() == 0 && s.willChange > 0, in.resultShown};
    info.currentStep = -1;
    for (int i = 0; i < 4; ++i) {
        if (!info.stepDone[i]) {
            info.currentStep = i;
            break;
        }
    }
    return info;
}

} // namespace ctrm::app
