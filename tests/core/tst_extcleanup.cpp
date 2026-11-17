// SPDX-License-Identifier: GPL-3.0-or-later
#include "model/nameparts.h"
#include "rules/cleanuprule.h"
#include "rules/extensionrule.h"

#include <QJsonObject>
#include <QtTest>

using namespace ctrm;

Q_DECLARE_METATYPE(ctrm::CleanUpRule::Spaces)

namespace {

RuleContext fileCtx()
{
    RuleContext ctx;
    ctx.count = 1;
    ctx.includedCount = 1;
    ctx.includedIndex = 0;
    return ctx;
}

// Applies the rule to a file name and joins the result.
QString applyTo(const Rule &rule, const QString &name, const RuleContext &ctx = fileCtx())
{
    NameParts parts = splitName(name, ctx.isFolder, ctx.folderExtensions);
    rule.apply(parts, ctx);
    return joinName(parts);
}

ExtensionRule extRule(ExtensionRule::Action action, const QString &value = {},
                      const QString &onlyIf = {})
{
    ExtensionRule r;
    r.action = action;
    r.value = value;
    r.onlyIfMatches = onlyIf;
    return r;
}

CleanUpRule cleanRule(Target target = Target::Stem)
{
    CleanUpRule r;
    r.target = target;
    return r;
}

QString name255()
{
    return QString(251, QLatin1Char('a')) + QStringLiteral(".TXT");
}

} // namespace

class TestExtCleanUp : public QObject
{
    Q_OBJECT
private slots:
    void extDefaults();
    void extLower();
    void extUpper();
    void extSetOnNoExtension();
    void extSetEmptyValueNoEffect();
    void extRemove();
    void extAdd();
    void extOnlyIfMatches();
    void extFolderNoEffect();
    void extFixedTarget();
    void cleanDefaults();
    void cleanTrimCollapse();
    void cleanUnicodeSpaces();
    void cleanSpacesTo_data();
    void cleanSpacesTo();
    void cleanOptionsOff();
    void cleanReplaceInvalidPortable();
    void cleanReplaceInvalidLinux();
    void cleanTargetName();
    void edgeInputs_data();
    void edgeInputs();
    void jsonRoundTrip();
    void badValueUsesDefault();
};

void TestExtCleanUp::extDefaults()
{
    const ExtensionRule r;
    QCOMPARE(r.type(), QStringLiteral("extension"));
    QCOMPARE(r.action, ExtensionRule::Action::Lower);
    QCOMPARE(r.target, Target::Extension);
    QVERIFY(r.value.isEmpty());
    QVERIFY(r.onlyIfMatches.isEmpty());
    QCOMPARE(r.optionKeys(), QStringList({QStringLiteral("action"), QStringLiteral("value"),
                                          QStringLiteral("onlyIfMatches")}));
}

void TestExtCleanUp::extLower()
{
    const auto r = extRule(ExtensionRule::Action::Lower);
    QCOMPARE(applyTo(r, QStringLiteral("a.JPG")), QStringLiteral("a.jpg"));
    QCOMPARE(applyTo(r, QStringLiteral("A.B.JPG")), QStringLiteral("A.B.jpg"));
    QCOMPARE(applyTo(r, QStringLiteral("a.\u00C9T\u00C9")), QStringLiteral("a.\u00E9t\u00E9"));
}

void TestExtCleanUp::extUpper()
{
    const auto r = extRule(ExtensionRule::Action::Upper);
    QCOMPARE(applyTo(r, QStringLiteral("a.jpg")), QStringLiteral("a.JPG"));
    QCOMPARE(applyTo(r, QStringLiteral("noext")), QStringLiteral("noext"));
}

void TestExtCleanUp::extSetOnNoExtension()
{
    QCOMPARE(applyTo(extRule(ExtensionRule::Action::Set, QStringLiteral("jpg")),
                     QStringLiteral("a")),
             QStringLiteral("a.jpg"));
    QCOMPARE(applyTo(extRule(ExtensionRule::Action::Set, QStringLiteral(".jpg")),
                     QStringLiteral("a")),
             QStringLiteral("a.jpg"));
    QCOMPARE(applyTo(extRule(ExtensionRule::Action::Set, QStringLiteral("jpg")),
                     QStringLiteral("a.jpeg")),
             QStringLiteral("a.jpg"));
    QCOMPARE(applyTo(extRule(ExtensionRule::Action::Set, QStringLiteral("jpg")),
                     QStringLiteral("file.")),
             QStringLiteral("file.jpg"));
    // Only one leading dot is ignored.
    QCOMPARE(applyTo(extRule(ExtensionRule::Action::Set, QStringLiteral("..x")),
                     QStringLiteral("a")),
             QStringLiteral("a..x"));
}

void TestExtCleanUp::extSetEmptyValueNoEffect()
{
    for (auto action : {ExtensionRule::Action::Set, ExtensionRule::Action::Add}) {
        for (const QString &value : {QString(), QStringLiteral(".")}) {
            const auto r = extRule(action, value);
            QCOMPARE(applyTo(r, QStringLiteral("a.jpg")), QStringLiteral("a.jpg"));
            QCOMPARE(applyTo(r, QStringLiteral("a")), QStringLiteral("a"));
        }
    }
}

void TestExtCleanUp::extRemove()
{
    const auto r = extRule(ExtensionRule::Action::Remove);
    QCOMPARE(applyTo(r, QStringLiteral("a.jpg")), QStringLiteral("a"));
    QCOMPARE(applyTo(r, QStringLiteral(".bashrc")), QStringLiteral(".bashrc"));
    QCOMPARE(applyTo(r, QStringLiteral("a.tar.gz")), QStringLiteral("a.tar"));
    QCOMPARE(applyTo(r, QStringLiteral("file.")), QStringLiteral("file"));
    QCOMPARE(applyTo(r, QStringLiteral("noext")), QStringLiteral("noext"));
}

void TestExtCleanUp::extAdd()
{
    const auto r = extRule(ExtensionRule::Action::Add, QStringLiteral("gz"));
    QCOMPARE(applyTo(r, QStringLiteral("a.tar")), QStringLiteral("a.tar.gz"));
    QCOMPARE(applyTo(r, QStringLiteral("a")), QStringLiteral("a.gz"));
    QCOMPARE(applyTo(r, QStringLiteral(".bashrc")), QStringLiteral(".bashrc.gz"));
    QCOMPARE(applyTo(extRule(ExtensionRule::Action::Add, QStringLiteral(".bak")),
                     QStringLiteral("a.txt")),
             QStringLiteral("a.txt.bak"));
}

void TestExtCleanUp::extOnlyIfMatches()
{
    const auto r =
            extRule(ExtensionRule::Action::Set, QStringLiteral("jpg"), QStringLiteral("jpeg;JPE"));
    QCOMPARE(applyTo(r, QStringLiteral("a.jpeg")), QStringLiteral("a.jpg"));
    QCOMPARE(applyTo(r, QStringLiteral("a.JPEG")), QStringLiteral("a.jpg"));
    QCOMPARE(applyTo(r, QStringLiteral("a.jpe")), QStringLiteral("a.jpg"));
    QCOMPARE(applyTo(r, QStringLiteral("a.png")), QStringLiteral("a.png"));
    QCOMPARE(applyTo(r, QStringLiteral("a")), QStringLiteral("a"));
    QCOMPARE(applyTo(r, QStringLiteral("a.jpeg2")), QStringLiteral("a.jpeg2"));

    // Spaces around entries and one leading dot are ignored; empty entries are ignored.
    const auto loose = extRule(ExtensionRule::Action::Lower, {}, QStringLiteral(" .TIF ; ;tiff"));
    QCOMPARE(applyTo(loose, QStringLiteral("a.TIF")), QStringLiteral("a.tif"));
    QCOMPARE(applyTo(loose, QStringLiteral("a.TIFF")), QStringLiteral("a.tiff"));
    QCOMPARE(applyTo(loose, QStringLiteral("a.PNG")), QStringLiteral("a.PNG"));

    // A list with only separators matches all items.
    const auto empty = extRule(ExtensionRule::Action::Lower, {}, QStringLiteral(" ; "));
    QCOMPARE(applyTo(empty, QStringLiteral("a.PNG")), QStringLiteral("a.png"));
}

void TestExtCleanUp::extFolderNoEffect()
{
    RuleContext ctx = fileCtx();
    ctx.isFolder = true;
    const auto upper = extRule(ExtensionRule::Action::Upper);
    QCOMPARE(applyTo(upper, QStringLiteral("Pro.v2"), ctx), QStringLiteral("Pro.v2"));
    QCOMPARE(applyTo(extRule(ExtensionRule::Action::Set, QStringLiteral("x")),
                     QStringLiteral("Pro"), ctx),
             QStringLiteral("Pro"));
    ctx.folderExtensions = true;
    QCOMPARE(applyTo(upper, QStringLiteral("Pro.v2"), ctx), QStringLiteral("Pro.V2"));
}

void TestExtCleanUp::extFixedTarget()
{
    ExtensionRule r;
    QVERIFY(r.hasFixedTarget());
    QVERIFY(!r.optionsToJson().contains(QStringLiteral("target")));
    QVERIFY(!r.optionKeys().contains(QStringLiteral("target")));
    // A target set by mistake has no effect: the rule still changes the extension.
    r.target = Target::Stem;
    r.action = ExtensionRule::Action::Upper;
    QCOMPARE(applyTo(r, QStringLiteral("ab.cd")), QStringLiteral("ab.CD"));
    QCOMPARE(r.clone()->type(), QStringLiteral("extension"));
}

void TestExtCleanUp::cleanDefaults()
{
    const CleanUpRule r;
    QCOMPARE(r.type(), QStringLiteral("cleanup"));
    QVERIFY(r.trim);
    QVERIFY(r.collapseSpaces);
    QCOMPARE(r.spacesTo, CleanUpRule::Spaces::Keep);
    QVERIFY(!r.replaceInvalid);
    QCOMPARE(r.replacement, QStringLiteral("_"));
    QVERIFY(!r.hasFixedTarget());
    QCOMPARE(r.optionKeys(),
             QStringList({QStringLiteral("trim"), QStringLiteral("collapseSpaces"),
                          QStringLiteral("spacesTo"), QStringLiteral("replaceInvalid"),
                          QStringLiteral("replacement")}));
}

void TestExtCleanUp::cleanTrimCollapse()
{
    const auto r = cleanRule();
    QCOMPARE(applyTo(r, QStringLiteral("  a   b  ")), QStringLiteral("a b"));
    QCOMPARE(applyTo(r, QStringLiteral("  a   b  .txt")), QStringLiteral("a b.txt"));
    QCOMPARE(applyTo(r, QStringLiteral("a b")), QStringLiteral("a b"));
}

void TestExtCleanUp::cleanUnicodeSpaces()
{
    const auto r = cleanRule();
    // Tab, NBSP and ideographic space are spaces.
    QCOMPARE(applyTo(r, QStringLiteral("\t\u00A0a\u3000\u3000b\u00A0")), QStringLiteral("a b"));
    // A single non-ASCII space inside is kept by collapse.
    QCOMPARE(applyTo(r, QStringLiteral("a\u00A0b")), QStringLiteral("a\u00A0b"));
    // A space with a combining mark is one cluster: the full cluster goes.
    QCOMPARE(applyTo(r, QStringLiteral("a \u0301")), QStringLiteral("a"));

    CleanUpRule under = cleanRule();
    under.spacesTo = CleanUpRule::Spaces::Underscore;
    QCOMPARE(applyTo(under, QStringLiteral("a\u00A0b\tc")), QStringLiteral("a_b_c"));
    QCOMPARE(applyTo(under, QStringLiteral("a \u0301b")), QStringLiteral("a_b"));
}

void TestExtCleanUp::cleanSpacesTo_data()
{
    QTest::addColumn<CleanUpRule::Spaces>("spaces");
    QTest::addColumn<QString>("expected");
    QTest::newRow("keep") << CleanUpRule::Spaces::Keep << QStringLiteral("a b c");
    QTest::newRow("underscore") << CleanUpRule::Spaces::Underscore << QStringLiteral("a_b_c");
    QTest::newRow("dash") << CleanUpRule::Spaces::Dash << QStringLiteral("a-b-c");
    QTest::newRow("dot") << CleanUpRule::Spaces::Dot << QStringLiteral("a.b.c");
    QTest::newRow("remove") << CleanUpRule::Spaces::Remove << QStringLiteral("abc");
}

void TestExtCleanUp::cleanSpacesTo()
{
    QFETCH(CleanUpRule::Spaces, spaces);
    QFETCH(QString, expected);
    CleanUpRule r = cleanRule();
    r.spacesTo = spaces;
    QCOMPARE(applyTo(r, QStringLiteral("a b c")), expected);
    // Trim and collapse run first: "  a  b c " gives the same result.
    QCOMPARE(applyTo(r, QStringLiteral("  a  b c ")), expected);
}

void TestExtCleanUp::cleanOptionsOff()
{
    CleanUpRule r = cleanRule();
    r.trim = false;
    r.collapseSpaces = false;
    QCOMPARE(applyTo(r, QStringLiteral("  a   b  ")), QStringLiteral("  a   b  "));
    r.spacesTo = CleanUpRule::Spaces::Underscore;
    QCOMPARE(applyTo(r, QStringLiteral(" a  b")), QStringLiteral("_a__b"));
    r.trim = true;
    QCOMPARE(applyTo(r, QStringLiteral(" a  b ")), QStringLiteral("a__b"));
}

void TestExtCleanUp::cleanReplaceInvalidPortable()
{
    CleanUpRule r = cleanRule();
    r.replaceInvalid = true;
    RuleContext ctx = fileCtx();
    ctx.portableNames = true;
    for (OsRules os : {OsRules::Linux, OsRules::MacOS, OsRules::Windows}) {
        ctx.os = os;
        QCOMPARE(applyTo(r, QStringLiteral("a:b?"), ctx), QStringLiteral("a_b_"));
    }
    r.replacement = QStringLiteral("-");
    QCOMPARE(applyTo(r, QStringLiteral("a<b>c|d*e\"f\\g"), ctx), QStringLiteral("a-b-c-d-e-f-g"));
    r.replacement = QString();
    QCOMPARE(applyTo(r, QStringLiteral("a?b"), ctx), QStringLiteral("ab"));
}

void TestExtCleanUp::cleanReplaceInvalidLinux()
{
    CleanUpRule r = cleanRule();
    r.replaceInvalid = true;
    RuleContext ctx = fileCtx();
    ctx.portableNames = false;
    ctx.os = OsRules::Linux;
    QCOMPARE(applyTo(r, QStringLiteral("a:b"), ctx), QStringLiteral("a:b"));
    ctx.os = OsRules::MacOS;
    QCOMPARE(applyTo(r, QStringLiteral("a:b"), ctx), QStringLiteral("a_b"));

    // replaceInvalid off: forbidden characters stay.
    r.replaceInvalid = false;
    ctx.portableNames = true;
    QCOMPARE(applyTo(r, QStringLiteral("a:b"), ctx), QStringLiteral("a:b"));
}

void TestExtCleanUp::cleanTargetName()
{
    CleanUpRule r = cleanRule(Target::Name);
    r.spacesTo = CleanUpRule::Spaces::Underscore;
    QCOMPARE(applyTo(r, QStringLiteral(" my  file .txt ")), QStringLiteral("my_file_.txt"));
    CleanUpRule ext = cleanRule(Target::Extension);
    QCOMPARE(applyTo(ext, QStringLiteral("a. t x t")), QStringLiteral("a.t x t"));
}

void TestExtCleanUp::edgeInputs_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<bool>("isFolder");
    QTest::addColumn<QString>("extLower");
    QTest::addColumn<QString>("extSetPng");
    QTest::addColumn<QString>("cleanUnderscore");

    QTest::newRow("empty") << QString() << false << QString() << QStringLiteral(".png")
                           << QString();
    QTest::newRow("dot-file") << QStringLiteral(".bash RC") << false << QStringLiteral(".bash RC")
                              << QStringLiteral(".bash RC.png") << QStringLiteral(".bash_RC");
    QTest::newRow("no extension") << QStringLiteral("my file") << false << QStringLiteral("my file")
                                  << QStringLiteral("my file.png") << QStringLiteral("my_file");
    QTest::newRow("folder") << QStringLiteral("My Dir.V2") << true << QStringLiteral("My Dir.V2")
                            << QStringLiteral("My Dir.V2") << QStringLiteral("My_Dir.V2");
    QTest::newRow("emoji") << QStringLiteral("\U0001F642 x.\U0001F642JPG") << false
                           << QStringLiteral("\U0001F642 x.\U0001F642jpg")
                           << QStringLiteral("\U0001F642 x.png")
                           << QStringLiteral("\U0001F642_x.\U0001F642JPG");
    QTest::newRow("combining") << QStringLiteral("cafe\u0301 a.E\u0301") << false
                               << QStringLiteral("cafe\u0301 a.e\u0301")
                               << QStringLiteral("cafe\u0301 a.png")
                               << QStringLiteral("cafe\u0301_a.E\u0301");
    QTest::newRow("255 chars") << name255() << false
                               << QString(251, QLatin1Char('a')) + QStringLiteral(".txt")
                               << QString(251, QLatin1Char('a')) + QStringLiteral(".png")
                               << name255();
}

void TestExtCleanUp::edgeInputs()
{
    QFETCH(QString, name);
    QFETCH(bool, isFolder);
    QFETCH(QString, extLower);
    QFETCH(QString, extSetPng);
    QFETCH(QString, cleanUnderscore);

    RuleContext ctx = fileCtx();
    ctx.isFolder = isFolder;
    QCOMPARE(applyTo(extRule(ExtensionRule::Action::Lower), name, ctx), extLower);
    QCOMPARE(applyTo(extRule(ExtensionRule::Action::Set, QStringLiteral("png")), name, ctx),
             extSetPng);
    CleanUpRule clean = cleanRule();
    clean.spacesTo = CleanUpRule::Spaces::Underscore;
    clean.replaceInvalid = true;
    QCOMPARE(applyTo(clean, name, ctx), cleanUnderscore);
}

void TestExtCleanUp::jsonRoundTrip()
{
    ExtensionRule e;
    e.action = ExtensionRule::Action::Add;
    e.value = QStringLiteral("gz");
    e.onlyIfMatches = QStringLiteral("tar");
    const QJsonObject ej = e.optionsToJson();
    QCOMPARE(ej.value(QStringLiteral("action")).toString(), QStringLiteral("add"));
    QCOMPARE(ej.keys().size(), 3);
    ExtensionRule e2;
    QStringList warnings;
    e2.optionsFromJson(ej, &warnings);
    QCOMPARE(e2.action, e.action);
    QCOMPARE(e2.value, e.value);
    QCOMPARE(e2.onlyIfMatches, e.onlyIfMatches);
    QVERIFY(warnings.isEmpty());

    const QList<QPair<ExtensionRule::Action, QString>> actions = {
            {ExtensionRule::Action::Set, QStringLiteral("set")},
            {ExtensionRule::Action::Remove, QStringLiteral("remove")},
            {ExtensionRule::Action::Lower, QStringLiteral("lower")},
            {ExtensionRule::Action::Upper, QStringLiteral("upper")},
            {ExtensionRule::Action::Add, QStringLiteral("add")},
    };
    for (const auto &[action, text] : actions) {
        e.action = action;
        QCOMPARE(e.optionsToJson().value(QStringLiteral("action")).toString(), text);
        e2.optionsFromJson(e.optionsToJson(), &warnings);
        QCOMPARE(e2.action, action);
    }

    const QList<QPair<CleanUpRule::Spaces, QString>> spaces = {
            {CleanUpRule::Spaces::Keep, QStringLiteral("keep")},
            {CleanUpRule::Spaces::Underscore, QStringLiteral("_")},
            {CleanUpRule::Spaces::Dash, QStringLiteral("-")},
            {CleanUpRule::Spaces::Dot, QStringLiteral(".")},
            {CleanUpRule::Spaces::Remove, QString()},
    };
    CleanUpRule c;
    c.trim = false;
    c.collapseSpaces = false;
    c.replaceInvalid = true;
    c.replacement = QStringLiteral("-");
    for (const auto &[mode, text] : spaces) {
        c.spacesTo = mode;
        const QJsonObject cj = c.optionsToJson();
        QCOMPARE(cj.keys().size(), 5);
        QVERIFY(cj.value(QStringLiteral("spacesTo")).isString());
        QCOMPARE(cj.value(QStringLiteral("spacesTo")).toString(), text);
        CleanUpRule c2;
        c2.optionsFromJson(cj, &warnings);
        QCOMPARE(c2.trim, false);
        QCOMPARE(c2.collapseSpaces, false);
        QCOMPARE(c2.spacesTo, mode);
        QCOMPARE(c2.replaceInvalid, true);
        QCOMPARE(c2.replacement, QStringLiteral("-"));
    }
    QVERIFY(warnings.isEmpty());
}

void TestExtCleanUp::badValueUsesDefault()
{
    ExtensionRule e;
    e.action = ExtensionRule::Action::Set;
    e.value = QStringLiteral("x");
    QStringList warnings;
    e.optionsFromJson(QJsonObject(), &warnings);
    QCOMPARE(e.action, ExtensionRule::Action::Lower);
    QVERIFY(e.value.isEmpty());
    QVERIFY(warnings.isEmpty());

    e.optionsFromJson(
            {{QStringLiteral("action"), QStringLiteral("explode")}, {QStringLiteral("value"), 5}},
            &warnings);
    QCOMPARE(e.action, ExtensionRule::Action::Lower);
    QVERIFY(e.value.isEmpty());
    QCOMPARE(warnings.size(), 2);

    CleanUpRule c;
    c.trim = false;
    warnings.clear();
    c.optionsFromJson({{QStringLiteral("spacesTo"), QStringLiteral("+")},
                       {QStringLiteral("trim"), QStringLiteral("yes")}},
                      &warnings);
    QCOMPARE(c.spacesTo, CleanUpRule::Spaces::Keep);
    QVERIFY(c.trim);
    QCOMPARE(warnings.size(), 2);
    // A null warnings pointer is allowed.
    c.optionsFromJson({{QStringLiteral("spacesTo"), 1}}, nullptr);
    QCOMPARE(c.spacesTo, CleanUpRule::Spaces::Keep);
}

QTEST_GUILESS_MAIN(TestExtCleanUp)
#include "tst_extcleanup.moc"
