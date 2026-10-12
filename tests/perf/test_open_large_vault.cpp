#include "ui/ExplorerPanel.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileSystemModel>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeView>

// F-002 / AV-005: a 1,000-file vault shows its tree in under 500 ms.
class TestOpenLargeVault : public QObject {
    Q_OBJECT

private slots:
    void thousandFileVaultLoadsUnderBudget()
    {
        constexpr int kFolders = 20;
        constexpr int kNotesPerFolder = 50;
        constexpr qint64 kBudgetMs = 500;

        QTemporaryDir vault;
        QVERIFY(vault.isValid());
        for (int f = 0; f < kFolders; ++f) {
            const QString folder = QStringLiteral("folder-%1").arg(f, 2, 10, QLatin1Char('0'));
            QVERIFY(QDir(vault.path()).mkdir(folder));
            for (int n = 0; n < kNotesPerFolder; ++n) {
                QFile note(vault.filePath(QStringLiteral("%1/note-%2.md").arg(folder).arg(n, 3, 10, QLatin1Char('0'))));
                QVERIFY(note.open(QIODevice::WriteOnly));
                note.write("# Note\n\nBody text.\n");
            }
        }

        coeus::ExplorerPanel explorer;
        explorer.resize(300, 800);
        explorer.show();

        QElapsedTimer timer;
        timer.start();

        QSignalSpy rootLoaded(&explorer, &coeus::ExplorerPanel::rootLoaded);
        explorer.setRootPath(vault.path());
        QVERIFY(rootLoaded.count() > 0 || rootLoaded.wait(kBudgetMs * 4));

        // Expanding every folder forces each listing to load.
        QFileSystemModel* model = explorer.model();
        const QModelIndex root = explorer.view()->rootIndex();
        QCOMPARE(model->rowCount(root), kFolders);
        int loaded = 0;
        connect(model, &QFileSystemModel::directoryLoaded, this, [&loaded](const QString&) { ++loaded; });
        explorer.view()->expandAll();
        QTRY_COMPARE_WITH_TIMEOUT(loaded, kFolders, int(kBudgetMs * 4));
        for (int r = 0; r < kFolders; ++r)
            QTRY_COMPARE(model->rowCount(model->index(r, 0, root)), kNotesPerFolder);

        const qint64 elapsed = timer.elapsed();
        qInfo("1,000-file vault: tree fully loaded in %lld ms (budget %lld ms)", elapsed, kBudgetMs);
        QVERIFY2(elapsed < kBudgetMs, qPrintable(QStringLiteral("took %1 ms").arg(elapsed)));
    }
};

QTEST_MAIN(TestOpenLargeVault)
#include "test_open_large_vault.moc"
