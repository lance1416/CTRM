// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QString>

namespace ctrm {

// Grapheme cluster boundaries as UTF-16 offsets. The first is 0 and the last is s.size().
QList<int> graphemeBoundaries(const QString &s);

// Number of grapheme clusters in s.
int graphemeCount(const QString &s);

// UTF-16 offset of grapheme graphemeIndex. Clamps to [0, s.size()].
int graphemeOffset(const QString &s, int graphemeIndex);

} // namespace ctrm
