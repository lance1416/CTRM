// SPDX-License-Identifier: GPL-3.0-or-later
#include "validate/namevalidator.h"

#include <QStringView>

#include <array>

namespace ctrm {

namespace {

constexpr int kMaxNameLength = 255;   // UTF-16 units (Windows) or UTF-8 bytes (macOS, Linux)
constexpr int kMacPathMax = 1023;     // PATH_MAX - 1 (PATH_MAX counts the NUL), UTF-8 bytes
constexpr int kLinuxPathMax = 4095;   // PATH_MAX - 1 (PATH_MAX counts the NUL), UTF-8 bytes
constexpr int kWindowsPathWarn = 260; // MAX_PATH, UTF-16 units

// Windows reserved device names (PRD 7.3, COM0 and LPT0 added by pm on 2026-11-17). U+00B9, U+00B2,
// U+00B3 are superscript 1, 2, 3.
const std::array<QStringView, 30> kReservedNames = {
        u"CON",  u"PRN",  u"AUX",       u"NUL",       u"COM0",      u"COM1",
        u"COM2", u"COM3", u"COM4",      u"COM5",      u"COM6",      u"COM7",
        u"COM8", u"COM9", u"COM\u00B9", u"COM\u00B2", u"COM\u00B3", u"LPT0",
        u"LPT1", u"LPT2", u"LPT3",      u"LPT4",      u"LPT5",      u"LPT6",
        u"LPT7", u"LPT8", u"LPT9",      u"LPT\u00B9", u"LPT\u00B2", u"LPT\u00B3",
};

bool isWindowsForbidden(char32_t c)
{
    if (c < 0x20)
        return true;
    switch (c) {
    case U'<':
    case U'>':
    case U':':
    case U'"':
    case U'/':
    case U'\\':
    case U'|':
    case U'?':
    case U'*':
        return true;
    default:
        return false;
    }
}

// Calls f(codePoint, utf16Offset, utf16Length) for each code point. An unpaired surrogate is
// passed as its own value with length 1.
template <typename F>
void forEachCodePoint(QStringView s, F f)
{
    for (qsizetype i = 0; i < s.size();) {
        const QChar c = s[i];
        if (c.isHighSurrogate() && i + 1 < s.size() && s[i + 1].isLowSurrogate()) {
            f(QChar::surrogateToUcs4(c, s[i + 1]), i, qsizetype(2));
            i += 2;
        } else {
            f(char32_t(c.unicode()), i, qsizetype(1));
            ++i;
        }
    }
}

Diagnostic makeDiag(DiagCode code, const QString &text = {}, int count = 0, int limit = 0)
{
    return Diagnostic{.code = code, .text = text, .count = count, .limit = limit};
}

int utf8Size(const QString &s)
{
    return int(s.toUtf8().size());
}

} // namespace

OsRules currentOsRules()
{
#if defined(Q_OS_WIN)
    return OsRules::Windows;
#elif defined(Q_OS_MACOS)
    return OsRules::MacOS;
#else
    return OsRules::Linux;
#endif
}

NameValidator::NameValidator(OsRules os, bool portableNames)
    : m_os(os)
    , m_portable(portableNames)
{}

bool NameValidator::windowsRules() const
{
    return m_os == OsRules::Windows || m_portable;
}

bool NameValidator::isForbidden(char32_t c) const
{
    if (c == U'\0' || c == U'/')
        return true;
    if (m_os == OsRules::MacOS && c == U':')
        return true;
    return windowsRules() && isWindowsForbidden(c);
}

std::optional<Diagnostic> NameValidator::checkName(const QString &name) const
{
    if (name.isEmpty())
        return makeDiag(DiagCode::EmptyName);
    if (name == u"." || name == u"..")
        return makeDiag(DiagCode::DotName);

    std::optional<Diagnostic> forbidden;
    forEachCodePoint(name, [&](char32_t c, qsizetype pos, qsizetype len) {
        if (!forbidden && isForbidden(c))
            forbidden = makeDiag(DiagCode::ForbiddenChar, name.mid(pos, len));
    });
    if (forbidden)
        return forbidden;

    if (windowsRules()) {
        // The text before the first dot, trailing spaces removed.
        QStringView base = QStringView(name).left(name.indexOf(u'.'));
        while (base.endsWith(u' '))
            base.chop(1);
        for (QStringView word : kReservedNames) {
            if (base.compare(word, Qt::CaseInsensitive) == 0)
                return makeDiag(DiagCode::ReservedName, word.toString());
        }
        if (name.endsWith(u' ') || name.endsWith(u'.'))
            return makeDiag(DiagCode::TrailingSpaceOrDot);
    }

    // UTF-8 bytes are never fewer than UTF-16 units, so check UTF-8 first where it applies.
    if (m_os != OsRules::Windows) {
        const int bytes = utf8Size(name);
        if (bytes > kMaxNameLength)
            return makeDiag(DiagCode::NameTooLong, QStringLiteral("utf8"), bytes, kMaxNameLength);
    }
    if (windowsRules() && name.size() > kMaxNameLength)
        return makeDiag(DiagCode::NameTooLong, QStringLiteral("utf16"), int(name.size()),
                        kMaxNameLength);

    return std::nullopt;
}

QList<Diagnostic> NameValidator::nameWarnings(const QString &name) const
{
    QList<Diagnostic> out;
    if (name.isEmpty() || name == u"." || name == u"..")
        return out;
    if (!windowsRules() && (name.endsWith(u' ') || name.endsWith(u'.')))
        out.append(makeDiag(DiagCode::EndsWithSpaceOrDot));
    if (name.front().isSpace() || name.back().isSpace())
        out.append(makeDiag(DiagCode::LeadingOrTrailingWhitespace));
    return out;
}

std::optional<Diagnostic> NameValidator::checkPath(const QString &folder, const QString &name) const
{
    int limit = 0;
    switch (m_os) {
    case OsRules::MacOS:
        limit = kMacPathMax;
        break;
    case OsRules::Linux:
        limit = kLinuxPathMax;
        break;
    case OsRules::Windows:
        return std::nullopt; // \\?\ paths: warning only (PRD 7.3)
    }
    const int bytes = utf8Size(folder) + 1 + utf8Size(name);
    if (bytes > limit)
        return makeDiag(DiagCode::PathTooLong, {}, bytes, limit);
    return std::nullopt;
}

QList<Diagnostic> NameValidator::pathWarnings(const QString &folder, const QString &name) const
{
    QList<Diagnostic> out;
    const int units = int(folder.size() + 1 + name.size());
    if (windowsRules() && units > kWindowsPathWarn)
        out.append(makeDiag(DiagCode::PathLongerThan260, {}, units, kWindowsPathWarn));
    return out;
}

QString NameValidator::replaceForbidden(const QString &name, const QString &replacement) const
{
    QString out;
    out.reserve(name.size());
    forEachCodePoint(name, [&](char32_t c, qsizetype pos, qsizetype len) {
        if (isForbidden(c))
            out += replacement;
        else
            out += QStringView(name).mid(pos, len);
    });
    return out;
}

} // namespace ctrm
