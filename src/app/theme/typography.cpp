// SPDX-License-Identifier: GPL-3.0-or-later
#include "typography.h"

#include <algorithm>

namespace ctrm::app {

namespace {

constexpr qreal kCaptionMinimumPt = 10.0;

struct Style
{
    qreal ratio;
    QFont::Weight weight;
};

Style styleOf(TypeToken t)
{
    switch (t) {
    case TypeToken::Display:
        return {1.54, QFont::Normal};
    case TypeToken::Heading:
        return {1.15, QFont::DemiBold};
    case TypeToken::LabelStrong:
        return {1.0, QFont::DemiBold};
    case TypeToken::Body:
        return {1.0, QFont::Normal};
    case TypeToken::Caption:
        return {0.85, QFont::Normal};
    }
    return {1.0, QFont::Normal};
}

} // namespace

namespace detail {

QFont typeStyle(TypeToken t, const QFont &base, bool captionMinimum)
{
    const Style style = styleOf(t);
    QFont f = base;
    f.setWeight(style.weight);
    if (base.pointSizeF() > 0) {
        qreal size = base.pointSizeF() * style.ratio;
        if (t == TypeToken::Caption && captionMinimum)
            size = std::max(kCaptionMinimumPt, size);
        f.setPointSizeF(size);
    } else if (base.pixelSize() > 0) {
        f.setPixelSize(std::max(1, qRound(base.pixelSize() * style.ratio)));
    }
    return f;
}

} // namespace detail

QFont typeStyle(TypeToken t, const QFont &base)
{
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    return detail::typeStyle(t, base, true);
#else
    return detail::typeStyle(t, base, false);
#endif
}

QFont tabularFigures(QFont f)
{
    f.setFeature(QFont::Tag("tnum"), 1);
    return f;
}

} // namespace ctrm::app
