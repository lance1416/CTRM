// SPDX-License-Identifier: GPL-3.0-or-later
#include "model/status.h"
#include "validate/namevalidator.h"

#include <QtTest>

using namespace ctrm;

Q_DECLARE_METATYPE(ctrm::OsRules)

namespace {

std::optional<DiagCode> codeOf(const std::optional<Diagnostic> &d)
{
    if (!d)
        return std::nullopt;
    return d->code;
}

bool hasWarning(const QList<Diagnostic> &list, DiagCode code)
{
    return std::any_of(list.cbegin(), list.cend(),
                       [code](const Diagnostic &d) { return d.code == code; });
}

// A folder path of exactly `bytes` UTF-8 bytes: "/" followed by 'f' characters.
QString folderOfBytes(int bytes)
{
    return QStringLiteral("/") + QString(bytes - 1, QLatin1Char('f'));
}

} // namespace

class TestNameValidator : public QObject
{
    Q_OBJECT
private slots:
    void forbiddenPortable_data();
    void forbiddenPortable();
    void reservedPortable_data();
    void reservedPortable();
    void reservedNotOnOtherOs();
    void linuxNotPortable();
    void macNotPortable();
    void windowsControlChars();
    void emptyAndDots();
    void trailingDot();
    void trailingSpaceAndDotPortable();
    void lengthUtf8();
    void lengthUtf16();
    void lengthPortableUsesUtf8();
    void pathLimits();
    void pathLimitMac();
    void pathWarningPortableOnly();
    void whitespaceWarning();
    void tableOrder();
    void replaceForbidden();
    void replaceForbiddenKeepsSurrogatePairs();
    void isForbidden();
    void currentOs();
};

void TestNameValidator::forbiddenPortable_data()
{
    QTest::addColumn<OsRules>("os");
    QTest::newRow("Windows") << OsRules::Windows;
    QTest::newRow("MacOS") << OsRules::MacOS;
    QTest::newRow("Linux") << OsRules::Linux;
}

void TestNameValidator::forbiddenPortable()
{
    QFETCH(OsRules, os);
    const NameValidator v(os, true);
    const auto d = v.checkName(QStringLiteral("a:b.txt"));
    QVERIFY(d);
    QCOMPARE(d->code, DiagCode::ForbiddenChar);
    QCOMPARE(d->text, QStringLiteral(":"));

    for (const char *name : {"a<b", "a>b", "a\"b", "a/b", "a\\b", "a|b", "a?b", "a*b"})
        QCOMPARE(codeOf(v.checkName(QString::fromUtf8(name))), DiagCode::ForbiddenChar);
    QCOMPARE(codeOf(v.checkName(QStringLiteral("a\tb"))), DiagCode::ForbiddenChar);
}

void TestNameValidator::reservedPortable_data()
{
    QTest::addColumn<OsRules>("os");
    QTest::newRow("Windows") << OsRules::Windows;
    QTest::newRow("MacOS") << OsRules::MacOS;
    QTest::newRow("Linux") << OsRules::Linux;
}

void TestNameValidator::reservedPortable()
{
    QFETCH(OsRules, os);
    const NameValidator v(os, true);

    const QList<QPair<QString, QString>> reserved = {
            {QStringLiteral("CON.txt"), QStringLiteral("CON")},
            {QStringLiteral("con"), QStringLiteral("CON")},
            {QStringLiteral("Com1.tar.gz"), QStringLiteral("COM1")},
            {QStringLiteral("LPT\u00B2"), QStringLiteral("LPT\u00B2")},
            {QStringLiteral("NUL .txt"), QStringLiteral("NUL")},
            {QStringLiteral("prn"), QStringLiteral("PRN")},
            {QStringLiteral("aux.c"), QStringLiteral("AUX")},
            {QStringLiteral("com9"), QStringLiteral("COM9")},
            {QStringLiteral("LPT1"), QStringLiteral("LPT1")},
            {QStringLiteral("com\u00B9.log"), QStringLiteral("COM\u00B9")},
            {QStringLiteral("COM\u00B3"), QStringLiteral("COM\u00B3")},
            {QStringLiteral("CON..txt"), QStringLiteral("CON")},
            {QStringLiteral("COM0.txt"), QStringLiteral("COM0")},
            {QStringLiteral("lpt0"), QStringLiteral("LPT0")},
    };
    for (const auto &[name, word] : reserved) {
        const auto d = v.checkName(name);
        QVERIFY2(d, qPrintable(name));
        QCOMPARE(d->code, DiagCode::ReservedName);
        QCOMPARE(d->text, word);
    }

    for (const char *name : {"CONSOLE.txt", "COM10", "LPT", "xCON", "CON_1.txt", "my.CON"})
        QVERIFY2(!v.checkName(QString::fromUtf8(name)), name);
}

void TestNameValidator::reservedNotOnOtherOs()
{
    QVERIFY(!NameValidator(OsRules::MacOS, false).checkName(QStringLiteral("CON.txt")));
    QCOMPARE(codeOf(NameValidator(OsRules::Windows, false).checkName(QStringLiteral("aux"))),
             DiagCode::ReservedName);
}

void TestNameValidator::linuxNotPortable()
{
    const NameValidator v(OsRules::Linux, false);
    QVERIFY(!v.checkName(QStringLiteral("a:b")));
    QVERIFY(!v.checkName(QStringLiteral("CON.txt")));
    QVERIFY(!v.checkName(QStringLiteral("a?b*c|d<e>f\"g\\h")));
    QVERIFY(!v.checkName(QStringLiteral("a\tb")));
    const auto d = v.checkName(QStringLiteral("a/b"));
    QVERIFY(d);
    QCOMPARE(d->code, DiagCode::ForbiddenChar);
    QCOMPARE(d->text, QStringLiteral("/"));
    QCOMPARE(codeOf(v.checkName(QString(QChar(u'a')) + QChar(u'\0'))), DiagCode::ForbiddenChar);
}

void TestNameValidator::macNotPortable()
{
    const NameValidator v(OsRules::MacOS, false);
    const auto d = v.checkName(QStringLiteral("a:b"));
    QVERIFY(d);
    QCOMPARE(d->code, DiagCode::ForbiddenChar);
    QCOMPARE(d->text, QStringLiteral(":"));
    QCOMPARE(codeOf(v.checkName(QStringLiteral("a/b"))), DiagCode::ForbiddenChar);
    QVERIFY(!v.checkName(QStringLiteral("a?b")));
    QVERIFY(!v.checkName(QStringLiteral("a\tb")));
}

void TestNameValidator::windowsControlChars()
{
    const NameValidator v(OsRules::Windows, false);
    for (char16_t c = 0; c < 0x20; ++c) {
        const QString name = QStringLiteral("a") + QChar(c) + QStringLiteral("b");
        const auto d = v.checkName(name);
        QVERIFY(d);
        QCOMPARE(d->code, DiagCode::ForbiddenChar);
        QCOMPARE(d->text, QString(QChar(c)));
    }
    QVERIFY(!v.checkName(QStringLiteral("a\u007Fb")));
}

void TestNameValidator::emptyAndDots()
{
    for (OsRules os : {OsRules::Windows, OsRules::MacOS, OsRules::Linux}) {
        for (bool portable : {false, true}) {
            const NameValidator v(os, portable);
            QCOMPARE(codeOf(v.checkName(QString())), DiagCode::EmptyName);
            QCOMPARE(codeOf(v.checkName(QStringLiteral("."))), DiagCode::DotName);
            QCOMPARE(codeOf(v.checkName(QStringLiteral(".."))), DiagCode::DotName);
            QVERIFY(!v.checkName(QStringLiteral("...a")));
            QVERIFY(!v.checkName(QStringLiteral(".hidden")));
        }
    }
}

void TestNameValidator::trailingDot()
{
    const NameValidator win(OsRules::Windows, false);
    QCOMPARE(codeOf(win.checkName(QStringLiteral("a."))), DiagCode::TrailingSpaceOrDot);
    QCOMPARE(codeOf(win.checkName(QStringLiteral("a "))), DiagCode::TrailingSpaceOrDot);
    QVERIFY(!hasWarning(win.nameWarnings(QStringLiteral("a.")), DiagCode::EndsWithSpaceOrDot));

    const NameValidator lnx(OsRules::Linux, false);
    QVERIFY(!lnx.checkName(QStringLiteral("a.")));
    QVERIFY(hasWarning(lnx.nameWarnings(QStringLiteral("a.")), DiagCode::EndsWithSpaceOrDot));
    QVERIFY(!lnx.checkName(QStringLiteral("a ")));
    QVERIFY(hasWarning(lnx.nameWarnings(QStringLiteral("a ")), DiagCode::EndsWithSpaceOrDot));
    QVERIFY(!hasWarning(lnx.nameWarnings(QStringLiteral("a.b")), DiagCode::EndsWithSpaceOrDot));

    const NameValidator mac(OsRules::MacOS, false);
    QVERIFY(!mac.checkName(QStringLiteral("a.")));
    QVERIFY(hasWarning(mac.nameWarnings(QStringLiteral("a.")), DiagCode::EndsWithSpaceOrDot));
}

void TestNameValidator::trailingSpaceAndDotPortable()
{
    for (OsRules os : {OsRules::MacOS, OsRules::Linux}) {
        const NameValidator v(os, true);
        QCOMPARE(codeOf(v.checkName(QStringLiteral("a."))), DiagCode::TrailingSpaceOrDot);
        QCOMPARE(codeOf(v.checkName(QStringLiteral("a "))), DiagCode::TrailingSpaceOrDot);
        QVERIFY(!hasWarning(v.nameWarnings(QStringLiteral("a.")), DiagCode::EndsWithSpaceOrDot));
    }
}

void TestNameValidator::lengthUtf8()
{
    const NameValidator v(OsRules::Linux, false);
    const auto d = v.checkName(QString(256, QLatin1Char('a')));
    QVERIFY(d);
    QCOMPARE(d->code, DiagCode::NameTooLong);
    QCOMPARE(d->count, 256);
    QCOMPARE(d->limit, 255);
    QCOMPARE(d->text, QStringLiteral("utf8"));

    const auto e = v.checkName(QString(128, QChar(u'\u00E9')));
    QVERIFY(e);
    QCOMPARE(e->code, DiagCode::NameTooLong);
    QCOMPARE(e->count, 256);
    QCOMPARE(e->text, QStringLiteral("utf8"));

    QVERIFY(!v.checkName(QString(255, QLatin1Char('a'))));

    const NameValidator mac(OsRules::MacOS, false);
    QCOMPARE(codeOf(mac.checkName(QString(128, QChar(u'\u00E9')))), DiagCode::NameTooLong);
}

void TestNameValidator::lengthUtf16()
{
    const NameValidator v(OsRules::Windows, false);
    const auto d = v.checkName(QString(256, QLatin1Char('a')));
    QVERIFY(d);
    QCOMPARE(d->code, DiagCode::NameTooLong);
    QCOMPARE(d->count, 256);
    QCOMPARE(d->limit, 255);
    QCOMPARE(d->text, QStringLiteral("utf16"));

    QString smileys;
    for (int i = 0; i < 127; ++i)
        smileys += QStringLiteral("\U0001F642");
    QCOMPARE(smileys.size(), 254);
    QVERIFY(!v.checkName(smileys));
    QVERIFY(!v.checkName(QString(255, QLatin1Char('a'))));
    // 128 "é" is 256 UTF-8 bytes but only 128 UTF-16 units.
    QVERIFY(!v.checkName(QString(128, QChar(u'\u00E9'))));
}

void TestNameValidator::lengthPortableUsesUtf8()
{
    // UTF-8 bytes are never fewer than UTF-16 units, so the UTF-8 limit decides.
    const NameValidator v(OsRules::Linux, true);
    const auto d = v.checkName(QString(256, QLatin1Char('a')));
    QVERIFY(d);
    QCOMPARE(d->code, DiagCode::NameTooLong);
    QCOMPARE(d->text, QStringLiteral("utf8"));
}

void TestNameValidator::pathLimits()
{
    const NameValidator lnx(OsRules::Linux, false);
    const QString folder = folderOfBytes(4090);
    const QString name = QString(10, QLatin1Char('n'));
    const auto d = lnx.checkPath(folder, name);
    QVERIFY(d);
    QCOMPARE(d->code, DiagCode::PathTooLong);
    QCOMPARE(d->count, 4101);
    QCOMPARE(d->limit, 4095);
    // PATH_MAX (4,096) counts the NUL: 4,095 bytes is valid, 4,096 bytes is Invalid.
    QVERIFY(!lnx.checkPath(folderOfBytes(4084), name));          // 4,084 + 1 + 10 = 4,095
    const auto atMax = lnx.checkPath(folderOfBytes(4085), name); // 4,096
    QVERIFY(atMax);
    QCOMPARE(atMax->code, DiagCode::PathTooLong);
    QCOMPARE(atMax->count, 4096);
    QCOMPARE(atMax->limit, 4095);
    // UTF-8 bytes, not UTF-16 units: 2,047 "é" are 4,094 bytes.
    QVERIFY(lnx.checkPath(QStringLiteral("/") + QString(2047, QChar(u'\u00E9')),
                          QStringLiteral("ab")));

    const NameValidator win(OsRules::Windows, false);
    const QString winFolder = QStringLiteral("C:/") + QString(253, QLatin1Char('f')); // 256 units
    const QString winName = QStringLiteral("abcd"); // 256+1+4 = 261
    QVERIFY(!win.checkPath(winFolder, winName));
    const QList<Diagnostic> warnings = win.pathWarnings(winFolder, winName);
    QCOMPARE(warnings.size(), 1);
    QCOMPARE(warnings.first().code, DiagCode::PathLongerThan260);
    QCOMPARE(warnings.first().count, 261);
    QCOMPARE(warnings.first().limit, 260);
    QVERIFY(win.pathWarnings(winFolder, QStringLiteral("abc")).isEmpty());
    QVERIFY(!win.checkPath(QStringLiteral("C:/") + QString(40000, QLatin1Char('f')), winName));
}

void TestNameValidator::pathLimitMac()
{
    const NameValidator mac(OsRules::MacOS, false);
    // PATH_MAX (1,024) counts the NUL: 1,023 bytes is valid, 1,024 bytes is Invalid.
    const auto d = mac.checkPath(folderOfBytes(1019), QStringLiteral("abcd")); // 1,024
    QVERIFY(d);
    QCOMPARE(d->code, DiagCode::PathTooLong);
    QCOMPARE(d->count, 1024);
    QCOMPARE(d->limit, 1023);
    QVERIFY(!mac.checkPath(folderOfBytes(1018), QStringLiteral("abcd"))); // 1,023
}

void TestNameValidator::pathWarningPortableOnly()
{
    const QString folder = QStringLiteral("/") + QString(300, QLatin1Char('f'));
    for (OsRules os : {OsRules::MacOS, OsRules::Linux}) {
        QVERIFY(NameValidator(os, false).pathWarnings(folder, QStringLiteral("a")).isEmpty());
        QVERIFY(hasWarning(NameValidator(os, true).pathWarnings(folder, QStringLiteral("a")),
                           DiagCode::PathLongerThan260));
    }
}

void TestNameValidator::whitespaceWarning()
{
    for (OsRules os : {OsRules::Windows, OsRules::MacOS, OsRules::Linux}) {
        for (bool portable : {false, true}) {
            const NameValidator v(os, portable);
            QVERIFY(!v.checkName(QStringLiteral(" a")));
            QVERIFY(hasWarning(v.nameWarnings(QStringLiteral(" a")),
                               DiagCode::LeadingOrTrailingWhitespace));
            QVERIFY(hasWarning(v.nameWarnings(QStringLiteral("\u3000a.txt")),
                               DiagCode::LeadingOrTrailingWhitespace));
            QVERIFY(hasWarning(v.nameWarnings(QStringLiteral("a.txt\u00A0")),
                               DiagCode::LeadingOrTrailingWhitespace));
            QVERIFY(!hasWarning(v.nameWarnings(QStringLiteral("a b.txt")),
                                DiagCode::LeadingOrTrailingWhitespace));
            QVERIFY(v.nameWarnings(QStringLiteral("plain.txt")).isEmpty());
        }
    }
    // A trailing space is Invalid on Windows; the whitespace warning still shows.
    QVERIFY(hasWarning(NameValidator(OsRules::Windows, false).nameWarnings(QStringLiteral("a ")),
                       DiagCode::LeadingOrTrailingWhitespace));
}

void TestNameValidator::tableOrder()
{
    const NameValidator v(OsRules::Linux, true);
    // Forbidden comes before reserved, reserved before trailing dot, trailing dot before length.
    QCOMPARE(codeOf(v.checkName(QStringLiteral("CON:"))), DiagCode::ForbiddenChar);
    QCOMPARE(codeOf(v.checkName(QStringLiteral("CON."))), DiagCode::ReservedName);
    QCOMPARE(codeOf(v.checkName(QString(300, QLatin1Char('a')) + QLatin1Char('.'))),
             DiagCode::TrailingSpaceOrDot);
    QCOMPARE(codeOf(v.checkName(QString(300, QLatin1Char('a')) + QLatin1Char('?'))),
             DiagCode::ForbiddenChar);
    // The first forbidden character in the name is reported.
    QCOMPARE(v.checkName(QStringLiteral("a?b:c"))->text, QStringLiteral("?"));
}

void TestNameValidator::replaceForbidden()
{
    const NameValidator portable(OsRules::Linux, true);
    QCOMPARE(portable.replaceForbidden(QStringLiteral("a:b?c"), QStringLiteral("_")),
             QStringLiteral("a_b_c"));
    QCOMPARE(portable.replaceForbidden(QStringLiteral("a\tb"), QString()), QStringLiteral("ab"));

    const NameValidator lnx(OsRules::Linux, false);
    QCOMPARE(lnx.replaceForbidden(QStringLiteral("a:b"), QStringLiteral("_")),
             QStringLiteral("a:b"));
    QCOMPARE(lnx.replaceForbidden(QStringLiteral("a/b"), QStringLiteral("-")),
             QStringLiteral("a-b"));
}

void TestNameValidator::replaceForbiddenKeepsSurrogatePairs()
{
    const NameValidator v(OsRules::Windows, false);
    QCOMPARE(v.replaceForbidden(QStringLiteral("\U0001F642:\U0001F642"), QStringLiteral("_")),
             QStringLiteral("\U0001F642_\U0001F642"));
    // An unpaired surrogate is kept as it is.
    const QString lone = QStringLiteral("a") + QChar(0xD800) + QStringLiteral("?b");
    const QString expected = QStringLiteral("a") + QChar(0xD800) + QStringLiteral("_b");
    QCOMPARE(v.replaceForbidden(lone, QStringLiteral("_")), expected);
}

void TestNameValidator::isForbidden()
{
    const NameValidator win(OsRules::Windows, false);
    QVERIFY(win.isForbidden(U'*'));
    QVERIFY(win.isForbidden(U'\x1F'));
    QVERIFY(!win.isForbidden(U' '));
    QVERIFY(!win.isForbidden(U'\x7F'));
    QVERIFY(!win.isForbidden(U'\U0001F642'));

    const NameValidator mac(OsRules::MacOS, false);
    QVERIFY(mac.isForbidden(U':'));
    QVERIFY(mac.isForbidden(U'\0'));
    QVERIFY(!mac.isForbidden(U'\x01'));
    QVERIFY(!mac.isForbidden(U'*'));
}

void TestNameValidator::currentOs()
{
#if defined(Q_OS_WIN)
    QCOMPARE(currentOsRules(), OsRules::Windows);
#elif defined(Q_OS_MACOS)
    QCOMPARE(currentOsRules(), OsRules::MacOS);
#else
    QCOMPARE(currentOsRules(), OsRules::Linux);
#endif
}

QTEST_GUILESS_MAIN(TestNameValidator)
#include "tst_namevalidator.moc"
