// SPDX-License-Identifier: GPL-3.0-or-later
#include "temptree.h"
#include "version.h"

#include <QtTest>

class TestSkeleton : public QObject
{
    Q_OBJECT
private slots:
    void versionIsSet();
    void appIdIsSet();
    void tempTreeChecksums();
};

void TestSkeleton::versionIsSet()
{
    QCOMPARE(QString(CTRM_VERSION), QStringLiteral("0.1.0"));
}

void TestSkeleton::appIdIsSet()
{
    QCOMPARE(QString(CTRM_APP_ID), QStringLiteral("app.ctrm.CTRM"));
}

void TestSkeleton::tempTreeChecksums()
{
    TempTree t;
    t.file("a.txt", "A");
    t.mkdir("sub");
    t.file("sub/b.txt", "B");
    const auto sums = t.checksums();
    QCOMPARE(sums.size(), 2);
    QCOMPARE(sums.value("a.txt"), TempTree::sha256(t.root() + "/a.txt"));
    QCOMPARE(sums.value("sub/b.txt"), TempTree::sha256(t.root() + "/sub/b.txt"));
}

QTEST_GUILESS_MAIN(TestSkeleton)
#include "tst_skeleton.moc"
