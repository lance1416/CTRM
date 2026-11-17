// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "controllers/batchcontroller.h"

#include <QDialog>

class QKeyEvent;

namespace ctrm::app {

// The stop dialog after a failed or cancelled run (ui-design 9.6). The user must choose:
// Escape does not close it.
class FailureDialog : public QDialog
{
    Q_OBJECT
public:
    FailureDialog(const FailureInfo &info, QWidget *parent);
    FailureChoice choice() const;

    static QString titleText(const FailureInfo &info);
    static QString bodyText(const FailureInfo &info);

signals:
    void showReportClicked();

public slots:
    void reject() override; // does nothing: Escape and the close button do not choose

private:
    FailureChoice m_choice = FailureChoice::RollBack;
};

} // namespace ctrm::app
