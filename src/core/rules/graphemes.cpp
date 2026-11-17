// SPDX-License-Identifier: GPL-3.0-or-later
#include "rules/graphemes.h"

#include <QTextBoundaryFinder>

namespace ctrm {

QList<int> graphemeBoundaries(const QString &s)
{
    QList<int> result{0};
    if (s.isEmpty())
        return result;
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, s);
    for (qsizetype pos = finder.toNextBoundary(); pos > 0; pos = finder.toNextBoundary())
        result.append(int(pos));
    if (result.last() != s.size())
        result.append(int(s.size()));
    return result;
}

int graphemeCount(const QString &s)
{
    return int(graphemeBoundaries(s).size()) - 1;
}

int graphemeOffset(const QString &s, int graphemeIndex)
{
    if (graphemeIndex <= 0)
        return 0;
    const QList<int> bounds = graphemeBoundaries(s);
    if (graphemeIndex >= bounds.size())
        return int(s.size());
    return bounds.at(graphemeIndex);
}

} // namespace ctrm
