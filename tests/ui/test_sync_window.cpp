// F-028 in the window, through real rclone (skipped without it): the first
// pass asks to join, the status bar tells how sync stands, conflicts
// reach it, and a note open with unsaved changes is never overwritten
// silently when sync brings a new version (AV-004).

#include "sync/RcloneProvider.hpp"
#include "sync/SyncService.hpp"
#include "ui/Editor.hpp"
#include "ui/MainWindow.hpp"
#include "ui/VaultSession.hpp"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QToolButton>

using namespace coeus;

namespace {

constexpr int kWaitMs = 20'000;

void write(const QString& path, const QByteArray& content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write(content);
}

QByteArray read(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray("<missing>");
}

QString rclone()
{
    const QString chosen = qEnvironmentVariable("COEUS_RCLONE");
    return chosen.isEmpty() ? QStandardPaths::findExecutable(QStringLiteral("rclone")) : chosen;
}

QAction* actionNamed(MainWindow& window, const QString& text)
{
    for (QAction* action : window.registeredActions())
        if (action->text().remove(QLatin1Char('&')) == text)
            return action;
    return nullptr;
}

} // namespace

class TestSyncWindow : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_settings;
    QTemporaryDir m_config;

private slots:
    void initTestCase()
    {
        qRegisterMetaType<coeus::SyncReport>();
        qRegisterMetaType<coeus::SyncError>();
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        write(m_config.filePath(QStringLiteral("rclone.conf")), "");
        qputenv("RCLONE_CONFIG", m_config.filePath(QStringLiteral("rclone.conf")).toUtf8());
    }

    void syncInTheWindow()
    {
        const QString program = rclone();
        if (program.isEmpty())
            QSKIP("rclone is not installed (or set COEUS_RCLONE)");
        QTemporaryDir vault, laptop, drive;
        const QString remote = drive.filePath(QStringLiteral("Coeus"));
        const auto note = [&vault](const char* path) { return vault.filePath(QLatin1String(path)); };
        write(note("a.md"), "# A\n\nfrom the desktop\n");
        write(note("b.md"), "# B\n");
        write(note("c.md"), "# C\n");
        write(note(".coeus/config.toml"), QStringLiteral("[sync]\nremote = \"%1\"\nrclone = \"%2\"\n").arg(remote, program).toUtf8());

        MainWindow window;
        window.resize(1100, 700);
        window.show();
        QSignalSpy opened(&window, &MainWindow::indexOpened);
        window.openVault(*Vault::open(vault.path()));
        QVERIFY(opened.wait(kWaitMs));
        SyncService* sync = window.session()->sync();
        QVERIFY(sync->isConfigured());
        QAction* now = actionNamed(window, QStringLiteral("Sync Now"));
        QVERIFY(now);
        QCOMPARE(now->shortcut(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Y));
        QVERIFY(actionNamed(window, QStringLiteral("Set Up Sync…")));

        // The first pass, by itself, asks to join the vault and the remote.
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QMessageBox*>(QStringLiteral("syncJoinPrompt")), SyncService::kFirstPassDelayMs + kWaitMs);
        QVERIFY(window.syncButton()->isVisible());
        QCOMPARE(window.syncButton()->text(), QStringLiteral("⇅ join the remote?"));
        QSignalSpy finished(sync, &SyncService::finished);
        window.findChild<QMessageBox*>(QStringLiteral("syncJoinPrompt"))->findChild<QPushButton*>(QStringLiteral("join"))->click();
        QVERIFY(finished.wait(kWaitMs));
        QVERIFY(window.syncButton()->text().startsWith(QStringLiteral("⇅ synced ")));
        QCOMPARE(read(remote + QStringLiteral("/a.md")), QByteArray("# A\n\nfrom the desktop\n"));

        // Another machine joins and edits a.md and b.md.
        RcloneProvider other(remote, program);
        const std::atomic_bool keepGoing = false;
        QVERIFY(other.sync(laptop.path(), SyncProvider::Mode::Resync, keepGoing));
        QThread::msleep(1100);
        write(laptop.filePath(QStringLiteral("a.md")), "# A\n\nfrom the laptop\n");
        write(laptop.filePath(QStringLiteral("b.md")), "# B\n\nfrom the laptop\n");
        QVERIFY(other.sync(laptop.path(), SyncProvider::Mode::Normal, keepGoing));

        // Here, a.md is open with unsaved typing and b.md was edited and saved.
        window.openNote(note("a.md"));
        Editor* editor = window.currentEditor();
        editor->moveCursor(QTextCursor::End);
        editor->insertPlainText(QStringLiteral("typed here, not saved\n"));
        write(note("b.md"), "# B\n\nfrom the desktop\n");
        QSignalSpy prompted(&window, &MainWindow::conflictPrompted);
        finished.clear();
        now->trigger();
        QVERIFY(finished.wait(kWaitMs));
        // a.md: the laptop's text arrives on disk; the typing is still in the
        // editor, which asks what to do (AV-002, AV-004).
        QCOMPARE(read(note("a.md")), QByteArray("# A\n\nfrom the laptop\n"));
        QTRY_VERIFY_WITH_TIMEOUT(prompted.contains(QVariantList{note("a.md")}), kWaitMs);
        QVERIFY(editor->toPlainText().endsWith(QStringLiteral("typed here, not saved\n")));
        // b.md changed on both: this machine's copy stays, the laptop's beside it.
        QCOMPARE(read(note("b.md")), QByteArray("# B\n\nfrom the desktop\n"));
        QCOMPARE(read(note("b.md.conflict1")), QByteArray("# B\n\nfrom the laptop\n"));
        QTRY_COMPARE(sync->conflicts().size(), 1);
        QVERIFY(window.syncButton()->text().contains(QStringLiteral("⚠ 1 conflict")));
        QVERIFY(window.syncButton()->toolTip().contains(QStringLiteral("b.md")));
        if (auto* box = window.findChild<QMessageBox*>(QStringLiteral("conflictPrompt")))
            box->close();
        window.syncButton()->click(); // with a conflict waiting, it lists them
        auto* listed = window.findChild<QMessageBox*>(QStringLiteral("syncConflicts"));
        QVERIFY(listed);
        QVERIFY(listed->informativeText().contains(QStringLiteral("b.md.conflict1")));
        listed->close();

        // Set Up Sync… with nothing turns sync off here.
        window.setUpSync();
        auto* dialog = window.findChild<QInputDialog*>(QStringLiteral("syncSetUp"));
        QVERIFY(dialog);
        QCOMPARE(dialog->textValue(), remote);
        dialog->setTextValue(QString());
        dialog->accept();
        QVERIFY(!sync->isConfigured());
        QTRY_VERIFY(!window.syncButton()->isVisible());
        QVERIFY(read(note(".coeus/config.toml")).contains("remote = \"\""));
        editor->document()->setModified(false); // let the window close without asking
    }
};

QTEST_MAIN(TestSyncWindow)
#include "test_sync_window.moc"
