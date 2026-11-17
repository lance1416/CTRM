// SPDX-License-Identifier: GPL-3.0-or-later
#include "model/item.h"
#include "model/nameparts.h"
#include "model/status.h"

#include <QJsonObject>
#include <QtTest>

using namespace ctrm;

class TestModel : public QObject
{
    Q_OBJECT
private slots:
    void splitName_data();
    void splitName();
    void joinRoundTrip_data();
    void joinRoundTrip();
    void joinAddsDotOnlyWhenNeeded();
    void blockingStatuses();
    void warningCodes();
    void sameFileStrong();
    void sameFileWeak();
    void identityJsonRoundTrip();
    void itemPath();
};

void TestModel::splitName_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<bool>("isFolder");
    QTest::addColumn<bool>("folderExt");
    QTest::addColumn<QString>("stem");
    QTest::addColumn<QString>("ext");
    QTest::addColumn<bool>("hasDot");

    QTest::newRow("photo.jpg") << "photo.jpg" << false << false << "photo" << "jpg" << true;
    QTest::newRow(".bashrc") << ".bashrc" << false << false << ".bashrc" << "" << false;
    QTest::newRow("file.") << "file." << false << false << "file" << "" << true;
    QTest::newRow("archive.tar.gz")
            << "archive.tar.gz" << false << false << "archive.tar" << "gz" << true;
    QTest::newRow("folder Project.v2")
            << "Project.v2" << true << false << "Project.v2" << "" << false;
    QTest::newRow("folder Project.v2 folderExt")
            << "Project.v2" << true << true << "Project" << "v2" << true;
    QTest::newRow("noext") << "noext" << false << false << "noext" << "" << false;
    QTest::newRow(".hidden.txt") << ".hidden.txt" << false << false << ".hidden" << "txt" << true;
    QTest::newRow("empty") << "" << false << false << "" << "" << false;
}

void TestModel::splitName()
{
    QFETCH(QString, name);
    QFETCH(bool, isFolder);
    QFETCH(bool, folderExt);
    QFETCH(QString, stem);
    QFETCH(QString, ext);
    QFETCH(bool, hasDot);

    const NameParts parts = ctrm::splitName(name, isFolder, folderExt);
    QCOMPARE(parts.stem, stem);
    QCOMPARE(parts.extension, ext);
    QCOMPARE(parts.hasDot, hasDot);
}

void TestModel::joinRoundTrip_data()
{
    splitName_data();
}

void TestModel::joinRoundTrip()
{
    QFETCH(QString, name);
    QFETCH(bool, isFolder);
    QFETCH(bool, folderExt);

    QCOMPARE(joinName(ctrm::splitName(name, isFolder, folderExt)), name);
}

void TestModel::joinAddsDotOnlyWhenNeeded()
{
    QCOMPARE(joinName({"a", "", false}), QStringLiteral("a"));
    QCOMPARE(joinName({"a", "", true}), QStringLiteral("a."));
    QCOMPARE(joinName({"a", "b", false}), QStringLiteral("a.b"));
    QCOMPARE(joinName({"a", "b", true}), QStringLiteral("a.b"));
}

void TestModel::blockingStatuses()
{
    QVERIFY(isBlocking(Status::Conflict));
    QVERIFY(isBlocking(Status::Invalid));
    QVERIFY(isBlocking(Status::Missing));
    QVERIFY(isBlocking(Status::RuleError));
    QVERIFY(!isBlocking(Status::Unchanged));
    QVERIFY(!isBlocking(Status::Changed));
    QVERIFY(!isBlocking(Status::CaseOnly));
}

void TestModel::warningCodes()
{
    QVERIFY(isWarning(DiagCode::BecomesHidden));
    QVERIFY(isWarning(DiagCode::BecomesVisible));
    QVERIFY(isWarning(DiagCode::ExtensionChanged));
    QVERIFY(isWarning(DiagCode::PathLongerThan260));
    QVERIFY(isWarning(DiagCode::EndsWithSpaceOrDot));
    QVERIFY(isWarning(DiagCode::LeadingOrTrailingWhitespace));
    QVERIFY(isWarning(DiagCode::WeakIdentity));
    QVERIFY(!isWarning(DiagCode::DuplicateInBatch));
    QVERIFY(!isWarning(DiagCode::ExistsInFolder));
    QVERIFY(!isWarning(DiagCode::EmptyName));
    QVERIFY(!isWarning(DiagCode::PathTooLong));
    QVERIFY(!isWarning(DiagCode::NotOnDisk));
    QVERIFY(!isWarning(DiagCode::IdentityChanged));
    QVERIFY(!isWarning(DiagCode::RuleFailed));
    QVERIFY(!isWarning(DiagCode::UnsupportedRule));
}

void TestModel::sameFileStrong()
{
    FileIdentity a;
    a.type = IdentityType::Strong;
    a.volume = 7;
    a.idHigh = 1;
    a.idLow = 42;
    a.size = 100;
    a.mtimeNs = 5;

    FileIdentity b = a;
    b.size = 200;
    b.mtimeNs = 6;
    QVERIFY(sameFile(a, b));

    FileIdentity c = a;
    c.idLow = 43;
    QVERIFY(!sameFile(a, c));

    FileIdentity d = a;
    d.volume = 8;
    QVERIFY(!sameFile(a, d));

    FileIdentity e = a;
    e.idHigh = 2;
    QVERIFY(!sameFile(a, e));
}

void TestModel::sameFileWeak()
{
    FileIdentity a;
    a.type = IdentityType::Weak;
    a.size = 100;
    a.mtimeNs = 1'700'000'000'123'456'789;

    FileIdentity b = a;
    QVERIFY(sameFile(a, b));

    FileIdentity otherSize = a;
    otherSize.size = 101;
    QVERIFY(!sameFile(a, otherSize));

    // A weak folder matches on its type only: another time is the same folder, a file is not.
    FileIdentity folder = a;
    folder.size = -1;
    FileIdentity folderNow = folder;
    folderNow.mtimeNs += 5;
    QVERIFY(sameFile(folder, folderNow));
    FileIdentity fileNow = folder;
    fileNow.size = 4096;
    QVERIFY(!sameFile(folder, fileNow));
    // An old journal has a folder time; a folder read now has none.
    FileIdentity folderRead = folder;
    folderRead.mtimeNs = 0;
    QVERIFY(sameFile(folder, folderRead));

    FileIdentity otherTime = a;
    otherTime.mtimeNs += 1;
    QVERIFY(!sameFile(a, otherTime));

    FileIdentity strong = a;
    strong.type = IdentityType::Strong;
    QVERIFY(!sameFile(a, strong));
    QVERIFY(!sameFile(strong, a));
}

void TestModel::identityJsonRoundTrip()
{
    FileIdentity strong;
    strong.type = IdentityType::Strong;
    strong.volume = 0xFFFF'FFFF'FFFF'FFFFull;
    strong.idHigh = 0x8000'0000'0000'0001ull;
    strong.idLow = 0x1234'5678'9ABC'DEF0ull;
    strong.size = 9'007'199'254'740'993; // not exact as a double
    strong.mtimeNs = -1'234'567'890'123'456'789;

    const QJsonObject strongJson = identityToJson(strong);
    QCOMPARE(strongJson.value("type").toString(), QStringLiteral("strong"));
    QCOMPARE(strongJson.value("volume").toString(), QStringLiteral("18446744073709551615"));
    QCOMPARE(strongJson.value("size").toString(), QStringLiteral("9007199254740993"));
    QCOMPARE(identityFromJson(strongJson), std::optional<FileIdentity>(strong));

    FileIdentity weak;
    weak.type = IdentityType::Weak;
    weak.size = -1;
    weak.mtimeNs = 1'700'000'000'123'456'789;
    const QJsonObject weakJson = identityToJson(weak);
    QCOMPARE(weakJson.value("type").toString(), QStringLiteral("weak"));
    QCOMPARE(identityFromJson(weakJson), std::optional<FileIdentity>(weak));

    QCOMPARE(identityFromJson(QJsonObject{{"type", "x"}}), std::nullopt);
    QCOMPARE(identityFromJson(QJsonObject{}), std::nullopt);

    QJsonObject badNumber = strongJson;
    badNumber.insert("idLow", "12x");
    QCOMPARE(identityFromJson(badNumber), std::nullopt);

    QJsonObject numberNotString = strongJson;
    numberNotString.insert("size", 5);
    QCOMPARE(identityFromJson(numberNotString), std::nullopt);

    QJsonObject negativeUnsigned = strongJson;
    negativeUnsigned.insert("volume", "-1");
    QCOMPARE(identityFromJson(negativeUnsigned), std::nullopt);
}

void TestModel::itemPath()
{
    Item item;
    item.folder = QStringLiteral("/a/b");
    item.name = QStringLiteral("c.txt");
    QCOMPARE(item.path(), QStringLiteral("/a/b/c.txt"));
}

QTEST_GUILESS_MAIN(TestModel)
#include "tst_model.moc"
