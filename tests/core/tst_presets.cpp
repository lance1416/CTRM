// SPDX-License-Identifier: GPL-3.0-or-later
#include "presets/preset.h"
#include "presets/presetstore.h"
#include "rules/changecaserule.h"
#include "rules/cleanuprule.h"
#include "rules/extensionrule.h"
#include "rules/insertrule.h"
#include "rules/numberingrule.h"
#include "rules/removerule.h"
#include "rules/replacerule.h"
#include "rules/ruleregistry.h"
#include "rules/unsupportedrule.h"
#include "validate/namevalidator.h"
#include "version.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace ctrm;

namespace {

const QByteArray kPrdExample = R"({
  "$schema": "https://ctrm.example/schema/preset-v1.json",
  "format": "ctrm-preset",
  "formatVersion": 1,
  "name": "Holiday photos",
  "description": "Lower-case extension, then number as Holiday_001.jpg",
  "createdWith": "CTRM 0.1.0",
  "options": {
    "portableNames": true,
    "folderExtensions": false,
    "autoResolveDuplicates": false
  },
  "rules": [
    { "type": "extension", "enabled": true, "action": "lower", "onlyIfMatches": "" },
    { "type": "extension", "enabled": true, "action": "set", "value": "jpg",
      "onlyIfMatches": "jpeg;jpe" },
    { "type": "numbering", "enabled": true, "target": "stem",
      "start": 1, "step": 1, "padding": "auto",
      "position": "replace", "separator": "_", "scope": "checked" },
    { "type": "insert", "enabled": true, "target": "stem",
      "text": "Holiday_", "position": "prefix" }
  ]
}
)";

QByteArray minimalPreset(const QByteArray &extra)
{
    return R"({"format": "ctrm-preset", "formatVersion": 1, "name": "M", "rules": [])" + extra +
           "}";
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// A chain with all 7 rule types and non-default options.
RuleChain fullChain()
{
    RuleChain chain;
    chain.options = ChainOptions{false, true, true};

    auto replace = std::make_unique<ReplaceRule>();
    replace->find = QStringLiteral("(\\d+)");
    replace->replace = QStringLiteral("<$1>");
    replace->mode = ReplaceRule::Mode::Regex;
    replace->occurrence = ReplaceRule::Occurrence::First;
    replace->target = Target::Name;
    chain.insert(chain.size(), std::move(replace));

    auto insert = std::make_unique<InsertRule>();
    insert->text = QStringLiteral("é🙂\"\\\t");
    insert->position = InsertRule::Position::Index;
    insert->index = 2;
    insert->fromEnd = true;
    insert->enabled = false;
    chain.insert(chain.size(), std::move(insert));

    auto remove = std::make_unique<RemoveRule>();
    remove->mode = RemoveRule::Mode::Characters;
    remove->classes = RemoveRule::Digits | RemoveRule::Custom;
    remove->custom = QStringLiteral("#");
    chain.insert(chain.size(), std::move(remove));

    auto cleanup = std::make_unique<CleanUpRule>();
    cleanup->spacesTo = CleanUpRule::Spaces::Dot;
    cleanup->replaceInvalid = true;
    chain.insert(chain.size(), std::move(cleanup));

    auto changeCase = std::make_unique<ChangeCaseRule>();
    changeCase->caseMode = ChangeCaseRule::Case::Title;
    changeCase->smallWords = QStringList{"of", "the"};
    changeCase->target = Target::Extension;
    chain.insert(chain.size(), std::move(changeCase));

    auto extension = std::make_unique<ExtensionRule>();
    extension->action = ExtensionRule::Action::Set;
    extension->value = QStringLiteral("jpg");
    extension->onlyIfMatches = QStringLiteral("jpeg;jpe");
    chain.insert(chain.size(), std::move(extension));

    auto numbering = std::make_unique<NumberingRule>();
    numbering->start = 0;
    numbering->padding = 3;
    numbering->position = NumberingRule::Position::Prefix;
    numbering->scope = NumberingRule::Scope::All;
    chain.insert(chain.size(), std::move(numbering));
    return chain;
}

// Applies the chain to the names in list order, with all items included.
QStringList applyChain(const RuleChain &chain, const QStringList &names)
{
    QStringList out;
    for (int i = 0; i < names.size(); ++i) {
        RuleContext ctx;
        ctx.index = i;
        ctx.includedIndex = i;
        ctx.count = int(names.size());
        ctx.includedCount = int(names.size());
        ctx.folderExtensions = chain.options.folderExtensions;
        ctx.portableNames = chain.options.portableNames;
        ctx.os = currentOsRules();
        out.append(chain.apply(names.at(i), ctx));
    }
    return out;
}

} // namespace

class TestPresets : public QObject
{
    Q_OBJECT
private slots:
    void saveLoadIdentical();
    void prdExampleLoads();
    void keyOrderAndIndent();
    void stringsEscaped();
    void unknownTypeLoads();
    void newerVersionRejected();
    void bomAccepted();
    void wrongFormatRejected();
    void badDocumentsRejected();
    void unknownTopKeyKept();
    void badValuesWarn();
    void presetToJsonObjectMatches();
    void builtInsLoad();
    void storeSaveListRemove();
    void storeFileNames();
    void storeRenameKeepsSingleFile();
    void storeBuiltInsReadOnly();
    void storeListsBrokenFile();
    void importValidatesFirst();
    void exportWritesPreset();
    void sameNamesAllPlatforms();
};

void TestPresets::saveLoadIdentical()
{
    Preset preset; // AC14.1
    preset.name = QStringLiteral("All rules");
    preset.description = QStringLiteral("Every rule type");
    preset.chain = fullChain();

    const QByteArray bytes = serializePreset(preset);
    const PresetParseResult parsed = parsePreset(bytes);
    QVERIFY2(parsed.preset.has_value(), qPrintable(parsed.error));
    QCOMPARE(parsed.warnings, QStringList());
    QCOMPARE(parsed.preset->name, preset.name);
    QCOMPARE(parsed.preset->description, preset.description);
    QCOMPARE(parsed.preset->createdWith, QStringLiteral("CTRM " CTRM_VERSION));
    QCOMPARE(parsed.preset->chain.options, preset.chain.options);
    QCOMPARE(chainRulesToJson(parsed.preset->chain), chainRulesToJson(preset.chain));
    QCOMPARE(serializePreset(*parsed.preset), bytes);
}

void TestPresets::prdExampleLoads()
{
    const PresetParseResult parsed = parsePreset(kPrdExample);
    QVERIFY2(parsed.preset.has_value(), qPrintable(parsed.error));
    QCOMPARE(parsed.warnings, QStringList());
    const Preset &p = *parsed.preset;
    QCOMPARE(p.name, QStringLiteral("Holiday photos"));
    QCOMPARE(p.description, QStringLiteral("Lower-case extension, then number as Holiday_001.jpg"));
    QCOMPARE(p.createdWith, QStringLiteral("CTRM 0.1.0"));
    QCOMPARE(p.chain.options, (ChainOptions{true, false, false}));
    QCOMPARE(p.chain.size(), 4);
    QCOMPARE(p.chain.at(0)->type(), QStringLiteral("extension"));
    QCOMPARE(p.chain.at(1)->type(), QStringLiteral("extension"));
    QCOMPARE(p.chain.at(2)->type(), QStringLiteral("numbering"));
    QCOMPARE(p.chain.at(3)->type(), QStringLiteral("insert"));
    const auto *ext = dynamic_cast<const ExtensionRule *>(p.chain.at(1));
    QVERIFY(ext);
    QCOMPARE(ext->value, QStringLiteral("jpg"));
    QCOMPARE(applyChain(p.chain, {"a.JPEG", "b.png"}),
             (QStringList{"Holiday_1.jpg", "Holiday_2.png"}));
}

void TestPresets::keyOrderAndIndent()
{
    Preset preset;
    preset.name = QStringLiteral("Golden");
    preset.description = QStringLiteral("d");
    preset.chain.insert(0, std::make_unique<ExtensionRule>());
    auto insert = std::make_unique<InsertRule>();
    insert->text = QStringLiteral("X");
    insert->enabled = false;
    insert->unknownKeys.insert(QStringLiteral("b"), true);
    insert->unknownKeys.insert(QStringLiteral("a"), QJsonObject{{"y", 2}, {"x", QJsonArray{}}});
    preset.chain.insert(1, std::move(insert));
    preset.unknownKeys.insert(QStringLiteral("zzz"), 1);
    preset.unknownKeys.insert(QStringLiteral("aaa"), QJsonArray{1.5, "a", QJsonObject{}});

    const QByteArray expected = "{\n"
                                "  \"$schema\": \"https://ctrm.example/schema/preset-v1.json\",\n"
                                "  \"format\": \"ctrm-preset\",\n"
                                "  \"formatVersion\": 1,\n"
                                "  \"name\": \"Golden\",\n"
                                "  \"description\": \"d\",\n"
                                "  \"createdWith\": \"CTRM " CTRM_VERSION "\",\n"
                                "  \"options\": {\n"
                                "    \"portableNames\": true,\n"
                                "    \"folderExtensions\": false,\n"
                                "    \"autoResolveDuplicates\": false\n"
                                "  },\n"
                                "  \"rules\": [\n"
                                "    {\n"
                                "      \"type\": \"extension\",\n"
                                "      \"enabled\": true,\n"
                                "      \"action\": \"lower\",\n"
                                "      \"value\": \"\",\n"
                                "      \"onlyIfMatches\": \"\"\n"
                                "    },\n"
                                "    {\n"
                                "      \"type\": \"insert\",\n"
                                "      \"enabled\": false,\n"
                                "      \"target\": \"stem\",\n"
                                "      \"text\": \"X\",\n"
                                "      \"position\": \"prefix\",\n"
                                "      \"index\": 0,\n"
                                "      \"fromEnd\": false,\n"
                                "      \"a\": {\n"
                                "        \"x\": [],\n"
                                "        \"y\": 2\n"
                                "      },\n"
                                "      \"b\": true\n"
                                "    }\n"
                                "  ],\n"
                                "  \"aaa\": [\n"
                                "    1.5,\n"
                                "    \"a\",\n"
                                "    {}\n"
                                "  ],\n"
                                "  \"zzz\": 1\n"
                                "}\n";
    QCOMPARE(serializePreset(preset), expected);

    Preset empty;
    const QByteArray emptyRules = serializePreset(empty);
    QVERIFY(emptyRules.contains("  \"rules\": []\n}\n"));
    QVERIFY(!emptyRules.startsWith("\xEF\xBB\xBF"));
    QVERIFY(!emptyRules.contains('\r'));
}

void TestPresets::stringsEscaped()
{
    Preset preset;
    preset.name = QStringLiteral("q\"b\\s/t\tn\nü🙂\u0001");
    const QByteArray bytes = serializePreset(preset);
    QVERIFY(bytes.contains(
            QStringLiteral("\"name\": \"q\\\"b\\\\s/t\\tn\\nü🙂\\u0001\"").toUtf8()));
    QCOMPARE(parsePreset(bytes).preset->name, preset.name);
}

void TestPresets::unknownTypeLoads()
{
    const QByteArray json = minimalPreset("").replace(
            R"("rules": [])", R"("rules": [{"type": "exifDate", "enabled": true, "format": "x"}])");
    const PresetParseResult parsed = parsePreset(json); // AC14.3
    QVERIFY2(parsed.preset.has_value(), qPrintable(parsed.error));
    QCOMPARE(parsed.preset->chain.size(), 1);
    QVERIFY(dynamic_cast<const UnsupportedRule *>(parsed.preset->chain.at(0)));
    QVERIFY(parsed.preset->chain.firstError().has_value());
    QVERIFY(parsed.preset->chain.firstError()->problem.unsupported);
    // Saved again, the rule keeps its keys.
    const QByteArray saved = serializePreset(*parsed.preset);
    QVERIFY(saved.contains("\"type\": \"exifDate\""));
    QVERIFY(saved.contains("\"format\": \"x\""));
}

void TestPresets::newerVersionRejected()
{
    QByteArray json = minimalPreset("");
    json.replace("\"formatVersion\": 1", "\"formatVersion\": 99");
    const PresetParseResult parsed = parsePreset(json); // AC14.4
    QVERIFY(!parsed.preset.has_value());
    QCOMPARE(parsed.error, QStringLiteral("This preset needs a newer CTRM (format version 99)."));
}

void TestPresets::bomAccepted()
{
    const PresetParseResult parsed = parsePreset("\xEF\xBB\xBF" + kPrdExample);
    QVERIFY2(parsed.preset.has_value(), qPrintable(parsed.error));
    QCOMPARE(parsed.preset->name, QStringLiteral("Holiday photos"));
}

void TestPresets::wrongFormatRejected()
{
    QByteArray json = minimalPreset("");
    json.replace("ctrm-preset", "x");
    const PresetParseResult parsed = parsePreset(json);
    QVERIFY(!parsed.preset.has_value());
    QVERIFY(!parsed.error.isEmpty());
}

void TestPresets::badDocumentsRejected()
{
    const QList<QByteArray> bad{
            "",
            "{",
            "[]",
            R"({"formatVersion": 1, "rules": []})",
            R"({"format": "ctrm-preset", "rules": []})",
            R"({"format": "ctrm-preset", "formatVersion": "1", "rules": []})",
            R"({"format": "ctrm-preset", "formatVersion": 1.5, "rules": []})",
            R"({"format": "ctrm-preset", "formatVersion": 0, "rules": []})",
            R"({"format": "ctrm-preset", "formatVersion": 1, "rules": {}})",
            R"({"format": "ctrm-preset", "formatVersion": 1, "rules": [5]})",
    };
    for (const QByteArray &json : bad) {
        const PresetParseResult parsed = parsePreset(json);
        QVERIFY2(!parsed.preset.has_value(), json.constData());
        QVERIFY2(!parsed.error.isEmpty(), json.constData());
    }
}

void TestPresets::unknownTopKeyKept()
{
    const PresetParseResult parsed = parsePreset(minimalPreset(R"(, "zzz": 1)"));
    QVERIFY(parsed.preset.has_value());
    QCOMPARE(parsed.warnings, QStringList{"Unknown key 'zzz' in preset"});
    QCOMPARE(parsed.preset->unknownKeys, (QJsonObject{{"zzz", 1}}));
    const QByteArray saved = serializePreset(*parsed.preset);
    QVERIFY(saved.endsWith("  \"rules\": [],\n  \"zzz\": 1\n}\n"));
}

void TestPresets::badValuesWarn()
{
    const PresetParseResult parsed = parsePreset(
            minimalPreset(R"(, "description": 5, "options": {"portableNames": "no", "q": 1})")
                    .replace(R"("rules": [])",
                             R"("rules": [{"type": "replace", "mode": "fuzzy", "k": 1}])"));
    QVERIFY(parsed.preset.has_value());
    QCOMPARE(parsed.warnings,
             (QStringList{"Bad value for 'description' in preset",
                          "Bad value for 'portableNames' in options", "Unknown key 'q' in options",
                          "Bad value for 'mode' in rule replace",
                          "Unknown key 'k' in rule replace"}));
    QCOMPARE(parsed.preset->description, QString());
    QCOMPARE(parsed.preset->chain.options, ChainOptions{});

    // Missing options and rules give the defaults.
    const PresetParseResult bare =
            parsePreset(R"({"format": "ctrm-preset", "formatVersion": 1, "name": "B"})");
    QVERIFY(bare.preset.has_value());
    QCOMPARE(bare.warnings, QStringList());
    QCOMPARE(bare.preset->chain.size(), 0);
    QCOMPARE(bare.preset->chain.options, ChainOptions{});

    const PresetParseResult noName =
            parsePreset(R"({"format": "ctrm-preset", "formatVersion": 1, "options": []})");
    QVERIFY(noName.preset.has_value());
    QCOMPARE(noName.preset->name, QString());
    QCOMPARE(noName.warnings,
             (QStringList{"Bad value for 'name' in preset", "Bad value for 'options' in preset"}));
}

void TestPresets::presetToJsonObjectMatches()
{
    Preset preset;
    preset.name = QStringLiteral("All rules");
    preset.chain = fullChain();
    preset.unknownKeys.insert(QStringLiteral("zzz"), 1);
    const QJsonObject object = presetToJsonObject(preset);
    QCOMPARE(object, QJsonDocument::fromJson(serializePreset(preset)).object());
    QCOMPARE(object.value("rules").toArray(), chainRulesToJson(preset.chain));
}

void TestPresets::builtInsLoad()
{
    const PresetStore store(QDir::tempPath() + QStringLiteral("/ctrm-unused"));
    const QList<PresetEntry> builtIns = store.builtIns();
    QStringList names;
    for (const PresetEntry &e : builtIns) {
        QVERIFY(e.builtIn);
        QVERIFY(e.filePath.startsWith(QStringLiteral(":/ctrm/presets/")));
        QVERIFY(e.filePath.endsWith(QStringLiteral(".ctrm.json")));
        const PresetParseResult parsed = store.load(e);
        QVERIFY2(parsed.preset.has_value(), qPrintable(e.filePath + ": " + parsed.error));
        QCOMPARE(parsed.warnings, QStringList());
        QCOMPARE(parsed.preset->name, e.name);
        QCOMPARE(parsed.preset->chain.options, ChainOptions{});
        QVERIFY(!parsed.preset->chain.firstError().has_value());
        names.append(e.name);
    }
    QCOMPARE(names, (QStringList{"Lower-case extensions", "Spaces to underscores",
                                 "Photo numbering (IMG_0001)", "Remove [...] tags"}));

    const auto chainOf = [&](int i) { return *store.load(builtIns.at(i)).preset; };
    QCOMPARE(applyChain(chainOf(0).chain, {"A.JPG", "b.Txt", ".bashrc"}),
             (QStringList{"A.jpg", "b.txt", ".bashrc"}));
    QCOMPARE(applyChain(chainOf(1).chain, {"  my   holiday .jpg"}), QStringList{"my_holiday.jpg"});
    QCOMPARE(applyChain(chainOf(2).chain, {"a.jpg", "b.jpg", "c.jpg"}),
             (QStringList{"IMG_0001.jpg", "IMG_0002.jpg", "IMG_0003.jpg"}));
    QCOMPARE(applyChain(chainOf(3).chain, {"Song [live] [2020].mp3"}), QStringList{"Song.mp3"});
}

void TestPresets::storeSaveListRemove()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString userDir = dir.path() + QStringLiteral("/presets"); // created on first save
    PresetStore store(userDir);
    QVERIFY(store.userPresets().isEmpty());

    Preset preset;
    preset.name = QStringLiteral("My: Preset");
    preset.chain = fullChain();
    QString error;
    const auto saved = store.save(preset, QString(), &error);
    QVERIFY2(saved.has_value(), qPrintable(error));
    QCOMPARE(saved->name, preset.name);
    QVERIFY(!saved->builtIn);
    QCOMPARE(saved->filePath, userDir + QStringLiteral("/My_ Preset.ctrm.json"));
    QCOMPARE(readFile(saved->filePath), serializePreset(preset));

    QList<PresetEntry> list = store.userPresets();
    QCOMPARE(list.size(), 1);
    QCOMPARE(list.at(0).name, preset.name);
    QCOMPARE(list.at(0).filePath, saved->filePath);
    QCOMPARE(chainRulesToJson(store.load(list.at(0)).preset->chain),
             chainRulesToJson(preset.chain));

    // Save to an existing file replaces its content.
    preset.description = QStringLiteral("changed");
    QVERIFY(store.save(preset, saved->filePath, &error).has_value());
    QCOMPARE(store.load(*saved).preset->description, QStringLiteral("changed"));
    QCOMPARE(store.userPresets().size(), 1);

    const auto renamed = store.rename(*saved, QStringLiteral("Renamed"), &error);
    QVERIFY2(renamed.has_value(), qPrintable(error));
    QCOMPARE(renamed->name, QStringLiteral("Renamed"));
    QCOMPARE(renamed->filePath, userDir + QStringLiteral("/Renamed.ctrm.json"));
    QVERIFY(!QFile::exists(saved->filePath));
    QCOMPARE(store.load(*renamed).preset->name, QStringLiteral("Renamed"));

    const auto copy = store.duplicate(*renamed, &error);
    QVERIFY2(copy.has_value(), qPrintable(error));
    QCOMPARE(copy->name, QStringLiteral("Renamed (2)"));
    QCOMPARE(copy->filePath, userDir + QStringLiteral("/Renamed (2).ctrm.json"));
    QCOMPARE(chainRulesToJson(store.load(*copy).preset->chain), chainRulesToJson(preset.chain));
    const auto copy2 = store.duplicate(*renamed, &error);
    QCOMPARE(copy2->name, QStringLiteral("Renamed (3)"));

    list = store.userPresets();
    QCOMPARE(list.size(), 3);
    QCOMPARE(list.at(0).name, QStringLiteral("Renamed"));
    QCOMPARE(list.at(1).name, QStringLiteral("Renamed (2)"));
    QCOMPARE(list.at(2).name, QStringLiteral("Renamed (3)"));

    QVERIFY(store.remove(*copy, &error));
    QVERIFY(!QFile::exists(copy->filePath));
    QCOMPARE(store.userPresets().size(), 2);
}

void TestPresets::storeFileNames()
{
    QTemporaryDir dir;
    PresetStore store(dir.path());
    QString error;
    const auto saveNamed = [&](const QString &name) {
        Preset p;
        p.name = name;
        return QFileInfo(store.save(p, QString(), &error)->filePath).fileName();
    };
    QCOMPARE(saveNamed("a<b>c:d\"e/f\\g|h?i*j"), QStringLiteral("a_b_c_d_e_f_g_h_i_j.ctrm.json"));
    QCOMPARE(saveNamed("same"), QStringLiteral("same.ctrm.json"));
    QCOMPARE(saveNamed("same"), QStringLiteral("same (2).ctrm.json"));
    QCOMPARE(saveNamed("same"), QStringLiteral("same (3).ctrm.json"));
    QCOMPARE(saveNamed(""), QStringLiteral("Preset.ctrm.json"));
    QCOMPARE(saveNamed("CON"), QStringLiteral("_CON.ctrm.json"));
    QCOMPARE(saveNamed(QString(300, QLatin1Char('x'))),
             QString(200, QLatin1Char('x')) + QStringLiteral(".ctrm.json"));

    // Natural order of names, case-insensitive.
    PresetStore natural(dir.path() + QStringLiteral("/n"));
    for (const QString &n : {QStringLiteral("a10"), QStringLiteral("B"), QStringLiteral("a2")}) {
        Preset p;
        p.name = n;
        QVERIFY(natural.save(p, QString(), &error).has_value());
    }
    QStringList naturalNames;
    for (const PresetEntry &e : natural.userPresets())
        naturalNames.append(e.name);
    QCOMPARE(naturalNames, (QStringList{"a2", "a10", "B"}));
}

void TestPresets::storeRenameKeepsSingleFile()
{
    QTemporaryDir dir;
    PresetStore store(dir.path());
    QString error;
    Preset p;
    p.name = QStringLiteral("a");
    const auto saved = store.save(p, QString(), &error);
    // Only the letter case changes. On a case-insensitive volume, the file must not be lost.
    const auto renamed = store.rename(*saved, QStringLiteral("A"), &error);
    QVERIFY2(renamed.has_value(), qPrintable(error));
    QCOMPARE(store.userPresets().size(), 1);
    QCOMPARE(store.load(*renamed).preset->name, QStringLiteral("A"));
    // Same name: no change, one file.
    const auto same = store.rename(*renamed, QStringLiteral("A"), &error);
    QVERIFY(same.has_value());
    QCOMPARE(store.userPresets().size(), 1);
}

void TestPresets::storeBuiltInsReadOnly()
{
    QTemporaryDir dir;
    PresetStore store(dir.path());
    const PresetEntry builtIn = store.builtIns().at(0);
    QString error;
    QVERIFY(!store.remove(builtIn, &error));
    QVERIFY(!error.isEmpty());
    error.clear();
    QVERIFY(!store.rename(builtIn, QStringLiteral("x"), &error).has_value());
    QVERIFY(!error.isEmpty());
    error.clear();
    QVERIFY(!store.save(*store.load(builtIn).preset, builtIn.filePath, &error).has_value());
    QVERIFY(!error.isEmpty());

    const auto copy = store.duplicate(builtIn, &error);
    QVERIFY2(copy.has_value(), qPrintable(error));
    QVERIFY(!copy->builtIn);
    QCOMPARE(copy->name, QStringLiteral("Lower-case extensions (2)"));
    QVERIFY(copy->filePath.startsWith(dir.path()));
    QCOMPARE(store.userPresets().size(), 1);
}

void TestPresets::storeListsBrokenFile()
{
    QTemporaryDir dir;
    QVERIFY(writeFile(dir.filePath("broken.ctrm.json"), "{"));
    QVERIFY(writeFile(dir.filePath("other.json"), "{}"));
    PresetStore store(dir.path());
    const QList<PresetEntry> list = store.userPresets();
    QCOMPARE(list.size(), 1);
    QCOMPARE(list.at(0).name, QStringLiteral("broken"));
    const PresetParseResult parsed = store.load(list.at(0));
    QVERIFY(!parsed.preset.has_value());
    QVERIFY(!parsed.error.isEmpty());
    QString error;
    QVERIFY(store.remove(list.at(0), &error)); // the user can remove a broken file

    PresetEntry missing{QStringLiteral("gone"), dir.filePath("gone.ctrm.json"), false};
    QVERIFY(!store.load(missing).preset.has_value());
    QVERIFY(!store.load(missing).error.isEmpty());
}

void TestPresets::importValidatesFirst()
{
    QTemporaryDir source;
    QTemporaryDir user;
    PresetStore store(user.path());
    QString error;

    QByteArray newer = minimalPreset("");
    newer.replace("\"formatVersion\": 1", "\"formatVersion\": 99");
    QVERIFY(writeFile(source.filePath("newer.ctrm.json"), newer));
    QVERIFY(!store.importFile(source.filePath("newer.ctrm.json"), &error).has_value());
    QVERIFY(!error.isEmpty());
    QVERIFY(QDir(user.path()).entryList(QDir::Files).isEmpty());

    error.clear();
    QVERIFY(!store.importFile(source.filePath("absent.ctrm.json"), &error).has_value());
    QVERIFY(!error.isEmpty());

    QVERIFY(writeFile(source.filePath("any name.json"), kPrdExample));
    const auto imported = store.importFile(source.filePath("any name.json"), &error);
    QVERIFY2(imported.has_value(), qPrintable(error));
    QCOMPARE(imported->name, QStringLiteral("Holiday photos"));
    QCOMPARE(imported->filePath, user.filePath("Holiday photos.ctrm.json"));
    QCOMPARE(readFile(imported->filePath), kPrdExample); // the file is copied as it is
    const auto again = store.importFile(source.filePath("any name.json"), &error);
    QCOMPARE(again->filePath, user.filePath("Holiday photos (2).ctrm.json"));
}

void TestPresets::exportWritesPreset()
{
    QTemporaryDir dir;
    PresetStore store(dir.filePath("user"));
    Preset preset;
    preset.name = QStringLiteral("Out");
    preset.chain = fullChain();
    QString error;
    const QString path = dir.filePath("out.ctrm.json");
    QVERIFY2(store.exportTo(preset, path, &error), qPrintable(error));
    QCOMPARE(readFile(path), serializePreset(preset));
    QVERIFY(!store.exportTo(preset, dir.filePath("no/such/dir/out.json"), &error));
    QVERIFY(!error.isEmpty());
}

void TestPresets::sameNamesAllPlatforms()
{
    // AC14.2: the same input names give the same new names on macOS, Windows and Linux.
    const QStringList input{"IMG 0001.JPG",
                            "photo.jpeg",
                            "My Holiday [2024].png",
                            "  spaced   name .txt",
                            "file.tar.gz",
                            ".bashrc",
                            "noext",
                            "UPPER.TXT",
                            "Ünïcödé Fïlé.MP3",
                            "emoji 🙂 x.png",
                            "éclair.doc",
                            "a [b] [c].txt",
                            "[tag] only",
                            "x.JPE",
                            "straße.JPEG",
                            "CamelCase_File-Name.Md",
                            "multi..dots..txt",
                            "trailing.",
                            "Song (live) [hd].mp4",
                            "日本語 ファイル.jpg"};
    QCOMPARE(input.size(), 20);

    const QList<QStringList> expected{
            // Lower-case extensions
            {"IMG 0001.jpg",
             "photo.jpeg",
             "My Holiday [2024].png",
             "  spaced   name .txt",
             "file.tar.gz",
             ".bashrc",
             "noext",
             "UPPER.txt",
             "Ünïcödé Fïlé.mp3",
             "emoji 🙂 x.png",
             "éclair.doc",
             "a [b] [c].txt",
             "[tag] only",
             "x.jpe",
             "straße.jpeg",
             "CamelCase_File-Name.md",
             "multi..dots..txt",
             "trailing.",
             "Song (live) [hd].mp4",
             "日本語 ファイル.jpg"},
            // Spaces to underscores
            {"IMG_0001.JPG",
             "photo.jpeg",
             "My_Holiday_[2024].png",
             "spaced_name.txt",
             "file.tar.gz",
             ".bashrc",
             "noext",
             "UPPER.TXT",
             "Ünïcödé_Fïlé.MP3",
             "emoji_🙂_x.png",
             "éclair.doc",
             "a_[b]_[c].txt",
             "[tag]_only",
             "x.JPE",
             "straße.JPEG",
             "CamelCase_File-Name.Md",
             "multi..dots..txt",
             "trailing.",
             "Song_(live)_[hd].mp4",
             "日本語_ファイル.jpg"},
            // Photo numbering (IMG_0001)
            {"IMG_0001.JPG", "IMG_0002.jpeg", "IMG_0003.png", "IMG_0004.txt", "IMG_0005.gz",
             "IMG_0006",     "IMG_0007",      "IMG_0008.TXT", "IMG_0009.MP3", "IMG_0010.png",
             "IMG_0011.doc", "IMG_0012.txt",  "IMG_0013",     "IMG_0014.JPE", "IMG_0015.JPEG",
             "IMG_0016.Md",  "IMG_0017.txt",  "IMG_0018.",    "IMG_0019.mp4", "IMG_0020.jpg"},
            // Remove [...] tags
            {"IMG 0001.JPG",
             "photo.jpeg",
             "My Holiday.png",
             "spaced name.txt",
             "file.tar.gz",
             ".bashrc",
             "noext",
             "UPPER.TXT",
             "Ünïcödé Fïlé.MP3",
             "emoji 🙂 x.png",
             "éclair.doc",
             "a.txt",
             "only",
             "x.JPE",
             "straße.JPEG",
             "CamelCase_File-Name.Md",
             "multi..dots..txt",
             "trailing.",
             "Song (live).mp4",
             "日本語 ファイル.jpg"},
            // PRD 9.1 example
            {"Holiday_01.jpg", "Holiday_02.jpg", "Holiday_03.png", "Holiday_04.txt",
             "Holiday_05.gz",  "Holiday_06",     "Holiday_07",     "Holiday_08.txt",
             "Holiday_09.mp3", "Holiday_10.png", "Holiday_11.doc", "Holiday_12.txt",
             "Holiday_13",     "Holiday_14.jpg", "Holiday_15.jpg", "Holiday_16.md",
             "Holiday_17.txt", "Holiday_18.",    "Holiday_19.mp4", "Holiday_20.jpg"},
    };

    const PresetStore store(QDir::tempPath() + QStringLiteral("/ctrm-unused"));
    QList<Preset> presets;
    for (const PresetEntry &e : store.builtIns())
        presets.append(*store.load(e).preset);
    presets.append(*parsePreset(kPrdExample).preset);
    QCOMPARE(presets.size(), expected.size());
    QStringList mismatches;
    for (qsizetype i = 0; i < presets.size(); ++i) {
        const QStringList actual = applyChain(presets.at(i).chain, input);
        for (qsizetype j = 0; j < input.size(); ++j) {
            if (actual.at(j) != expected.at(i).at(j)) {
                mismatches.append(QStringLiteral("%1 / '%2': got '%3', want '%4'")
                                          .arg(presets.at(i).name, input.at(j), actual.at(j),
                                               expected.at(i).at(j)));
            }
        }
    }
    QCOMPARE(mismatches.join(QLatin1Char('\n')), QString());
}

QTEST_GUILESS_MAIN(TestPresets)
#include "tst_presets.moc"
