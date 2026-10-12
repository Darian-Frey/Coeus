// AV-002 / F-011 end to end: external edits reach open tabs through the
// watcher and index; a note with unsaved changes prompts rather than saves;
// unsaved work survives a restart through the journal.

#include "ui/Editor.hpp"
#include "ui/MainWindow.hpp"
#include "vault/Journal.hpp"

#include <QElapsedTimer>
#include <QFile>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace coeus;

namespace {

constexpr int kWaitMs = 5000;

void write(const QString& path, const QByteArray& content)
{
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write(content);
}

QByteArray read(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// Replaces the buffer the way typing does; setPlainText would clear the
// document's modified flag.
void type(Editor* editor, const QString& text)
{
    editor->selectAll();
    editor->insertPlainText(text);
}

void click(QWidget* window, const char* box, const char* button)
{
    window->findChild<QMessageBox*>(QLatin1String(box))->findChild<QPushButton*>(QLatin1String(button))->click();
}

} // namespace

class TestConflictPrompt : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_settings;

    // Opens `vault` and waits for its index, so the watcher is running.
    static void openAndWait(MainWindow& window, const QString& vault)
    {
        QSignalSpy opened(&window, &MainWindow::indexOpened);
        window.openVault(*Vault::open(vault));
        QVERIFY(opened.wait(kWaitMs));
    }

private slots:
    void initTestCase()
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    }

    void unmodifiedTabFollowsExternalEditsWithinASecond()
    {
        // F-011 acceptance: within one second.
        QTemporaryDir vault;
        const QString path = vault.filePath(QStringLiteral("n.md"));
        write(path, "before\n");
        MainWindow window;
        openAndWait(window, vault.path());
        window.openNote(path);

        QElapsedTimer timer;
        timer.start();
        write(path, "after\n");
        QTRY_COMPARE_WITH_TIMEOUT(window.currentEditor()->toPlainText(), QStringLiteral("after\n"), kWaitMs);
        QVERIFY2(timer.elapsed() < 1000, qPrintable(QStringLiteral("took %1 ms").arg(timer.elapsed())));
    }

    void externalEditOfAModifiedNotePrompts()
    {
        QTemporaryDir vault;
        const QString path = vault.filePath(QStringLiteral("n.md"));
        write(path, "base\n");
        MainWindow window;
        openAndWait(window, vault.path());
        window.openNote(path);
        Editor* editor = window.currentEditor();
        type(editor, QStringLiteral("my unsaved work\n"));

        QSignalSpy prompted(&window, &MainWindow::conflictPrompted);
        write(path, "their change\n");
        QVERIFY(prompted.wait(kWaitMs));
        QCOMPARE(read(path), QByteArray("their change\n")); // nothing clobbered
        QCOMPARE(editor->toPlainText(), QStringLiteral("my unsaved work\n")); // nor lost
        QVERIFY(editor->inConflict());

        click(&window, "conflictPrompt", "disk");
        QCOMPARE(editor->toPlainText(), QStringLiteral("their change\n"));
        QVERIFY(!editor->isModified());
    }

    void savingOverAnExternalChangePromptsAndKeepMineSaves()
    {
        QTemporaryDir vault;
        const QString path = vault.filePath(QStringLiteral("n.md"));
        write(path, "base\n");
        MainWindow window;
        window.openVault(*Vault::open(vault.path()));
        window.openNote(path);
        type(window.currentEditor(), QStringLiteral("mine\n"));
        write(path, "theirs\n");

        QSignalSpy prompted(&window, &MainWindow::conflictPrompted);
        QVERIFY(!window.saveCurrent());
        QCOMPARE(prompted.size(), 1);
        QCOMPARE(read(path), QByteArray("theirs\n"));

        click(&window, "conflictPrompt", "mine"); // keep mine, and the save goes ahead
        QCOMPARE(read(path), QByteArray("mine\n"));
        QVERIFY(!window.currentEditor()->isModified());
    }

    void unsavedWorkIsRecoveredAfterACrash()
    {
        QTemporaryDir vault;
        const QString path = vault.filePath(QStringLiteral("n.md"));
        write(path, "saved\n");
        {
            MainWindow window;
            window.openVault(*Vault::open(vault.path()));
            window.openNote(path);
            type(window.currentEditor(), QStringLiteral("typed but never saved\n"));
            window.journalNow();
            window.waitForJournal();
            QVERIFY(QFile::exists(Journal::fileFor(vault.path(), path)));
            // A crash: the window goes away without closeEvent or saving.
            window.currentEditor()->document()->setModified(false);
        }
        QCOMPARE(read(path), QByteArray("saved\n"));

        MainWindow window;
        QSignalSpy offered(&window, &MainWindow::recoveryOffered);
        window.openVault(*Vault::open(vault.path()));
        QVERIFY(offered.wait(kWaitMs));
        QCOMPARE(offered.front().front().toStringList(), QStringList{path});

        click(&window, "recoveryPrompt", "recover");
        Editor* editor = window.editorFor(path);
        QVERIFY(editor);
        QCOMPARE(editor->toPlainText(), QStringLiteral("typed but never saved\n"));
        QVERIFY(editor->isModified());
        QCOMPARE(read(path), QByteArray("saved\n")); // nothing written until saved

        QVERIFY(window.saveCurrent());
        QCOMPARE(read(path), QByteArray("typed but never saved\n"));
        window.waitForJournal();
        QVERIFY(!QFile::exists(Journal::fileFor(vault.path(), path))); // cleared by the save
    }

    void closingAModifiedTabAsks()
    {
        QTemporaryDir vault;
        const QString path = vault.filePath(QStringLiteral("n.md"));
        write(path, "x\n");
        MainWindow window;
        window.openVault(*Vault::open(vault.path()));
        window.openNote(path);
        type(window.currentEditor(), QStringLiteral("changed\n"));

        // The prompt is modal: answer it from the event loop.
        auto answer = [&window](QMessageBox::StandardButton button) {
            QTimer::singleShot(0, &window, [&window, button] {
                auto* box = window.findChild<QMessageBox*>(QStringLiteral("closePrompt"));
                if (box)
                    box->button(button)->click();
            });
        };
        answer(QMessageBox::Cancel);
        window.closeCurrentTab();
        QCOMPARE(window.tabs()->count(), 1);

        answer(QMessageBox::Save);
        window.closeCurrentTab();
        QCOMPARE(window.tabs()->count(), 0);
        QCOMPARE(read(path), QByteArray("changed\n"));
    }
};

QTEST_MAIN(TestConflictPrompt)
#include "test_conflict_prompt.moc"
