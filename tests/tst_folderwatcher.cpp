// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "auc/FolderWatcher.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace auc;

namespace {
void writeFile(const QString& path, const QByteArray& content)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(content);
}
}  // namespace

class TestFolderWatcher : public QObject {
    Q_OBJECT
private slots:
    void reportsNewChangedAndRemovedFiles()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath("existing.auc"), "x");
        writeFile(dir.filePath("ignored.txt"), "x");

        FolderWatcher w;
        w.setSettleTime(50);
        QVERIFY(w.watch(dir.path()));
        QCOMPARE(w.files().size(), 1);

        QSignalSpy added(&w, &FolderWatcher::fileAdded);
        QSignalSpy changed(&w, &FolderWatcher::fileChanged);
        QSignalSpy removed(&w, &FolderWatcher::fileRemoved);

        writeFile(dir.filePath("new.auc"), "hello");
        QTRY_COMPARE_WITH_TIMEOUT(added.count(), 1, 5000);
        QVERIFY(added.first().first().toString().endsWith("new.auc"));

        writeFile(dir.filePath("existing.auc"), "much longer content");
        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 1, 5000);

        QVERIFY(QFile::remove(dir.filePath("new.auc")));
        QTRY_COMPARE_WITH_TIMEOUT(removed.count(), 1, 5000);
    }

    void watchesNewSubfolders()
    {
        QTemporaryDir dir;
        FolderWatcher w;
        w.setSettleTime(50);
        QVERIFY(w.watch(dir.path()));
        QSignalSpy added(&w, &FolderWatcher::fileAdded);
        QVERIFY(QDir(dir.path()).mkdir("run2"));
        QTest::qWait(300);
        writeFile(dir.filePath("run2/scan.auc"), "abc");
        QTRY_COMPARE_WITH_TIMEOUT(added.count(), 1, 5000);
    }
};

QTEST_GUILESS_MAIN(TestFolderWatcher)
#include "tst_folderwatcher.moc"
