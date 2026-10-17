// F-036 in the window: File › New Note… names a note, places it and fills it
// from a template, which may choose the folder; nothing is ever overwritten.

#include "ui/Editor.hpp"
#include "ui/MainWindow.hpp"
#include "ui/NewNoteDialog.hpp"
#include "ui/NoteCatalogue.hpp"
#include "ui/NoteView.hpp"
#include "ui/PropertiesPanel.hpp"

#include <QAction>
#include <QComboBox>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace coeus;

namespace {

constexpr int kWaitMs = 10'000;

void write(const QString& path, const QByteArray& content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write(content);
}

QString read(const QString& path)
{
    QFile file(path);
    file.open(QIODevice::ReadOnly);
    return QString::fromUtf8(file.readAll());
}

QAction* actionNamed(MainWindow& window, const QString& text)
{
    for (QAction* action : window.registeredActions())
        if (action->text().remove(QLatin1Char('&')) == text)
            return action;
    return nullptr;
}

const QByteArray kHeader = "> **Status:** Active\n> **Provenance:** T\n> **Last reviewed:** 2026-10-15\n> **Why this status:** T\n\n# Proj\n";

} // namespace

class TestNewNote : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_settings;
    QTemporaryDir m_vault;
    std::unique_ptr<MainWindow> m_window;

    NewNoteDialog* openDialog()
    {
        QAction* action = actionNamed(*m_window, QStringLiteral("New Note…"));
        if (!action)
            return nullptr;
        action->trigger();
        return m_window->findChild<NewNoteDialog*>(QStringLiteral("newNoteDialog"));
    }

private slots:
    void initTestCase()
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        write(m_vault.filePath(QStringLiteral("proj/README.md")), kHeader);
        write(m_vault.filePath(QStringLiteral("proj/DECISIONS.md")), "# Decisions\n\n### D-001 First\n**Status:** Accepted\n");
        write(m_vault.filePath(QStringLiteral("proj/notes/today.md")), "Notes.\n");
        write(m_vault.filePath(QStringLiteral("templates/Meeting.md")),
              "---\ncoeus-folder: meetings\ntags: [meeting]\n---\n# {{title}}\n\nHeld {{date}}.\n");
        write(m_vault.filePath(QStringLiteral("templates/Decision note.md")), "# {{title}}\n\nRecorded as {{next:D}}.\n");
        m_window = std::make_unique<MainWindow>();
        m_window->resize(1000, 700);
        m_window->show();
        QSignalSpy reloaded(m_window->catalogue(), &NoteCatalogue::reloaded);
        m_window->openVault(*Vault::open(m_vault.path()));
        QVERIFY(reloaded.wait(kWaitMs));
    }

    void blankNoteInTheCurrentNotesFolder()
    {
        m_window->openNote(m_vault.filePath(QStringLiteral("proj/notes/today.md")));
        QAction* action = actionNamed(*m_window, QStringLiteral("New Note…"));
        QVERIFY(action);
        QCOMPARE(action->shortcut(), QKeySequence(QKeySequence::New));
        NewNoteDialog* dialog = openDialog();
        QVERIFY(dialog);
        QCOMPARE(dialog->folderEdit()->text(), QStringLiteral("proj/notes")); // the current note's folder
        QCOMPARE(dialog->templateBox()->currentText(), QStringLiteral("Blank"));
        QCOMPARE(dialog->templateBox()->count(), 3);
        QVERIFY(!dialog->okButton()->isEnabled()); // no name yet

        dialog->nameEdit()->setText(QStringLiteral("today"));
        QVERIFY(!dialog->okButton()->isEnabled()); // never overwritten
        QVERIFY(dialog->message().contains(QLatin1String("already exists")));
        dialog->nameEdit()->setText(QStringLiteral("a/b"));
        QVERIFY(!dialog->okButton()->isEnabled());

        dialog->nameEdit()->setText(QStringLiteral("Ideas"));
        QVERIFY(dialog->okButton()->isEnabled());
        QCOMPARE(dialog->message(), QStringLiteral("Creates proj/notes/Ideas.md"));
        QSignalSpy created(m_window.get(), &MainWindow::noteCreated);
        dialog->okButton()->click();
        QVERIFY(created.count() == 1 || created.wait(kWaitMs));
        const QString path = m_vault.filePath(QStringLiteral("proj/notes/Ideas.md"));
        QCOMPARE(created.front().front().toString(), path);
        QCOMPARE(read(path), QStringLiteral("# Ideas\n"));
        QCOMPARE(m_window->currentEditor()->path(), path);
    }

    void templateChoosesItsFolder()
    {
        NewNoteDialog* dialog = openDialog();
        QVERIFY(dialog);
        dialog->nameEdit()->setText(QStringLiteral("Standup"));
        dialog->templateBox()->setCurrentIndex(2); // Meeting → meetings/
        QVERIFY(dialog->templateBox()->currentText().startsWith(QLatin1String("Meeting")));
        QCOMPARE(dialog->folderEdit()->text(), QStringLiteral("meetings"));
        QCOMPARE(dialog->message(), QStringLiteral("Creates meetings/Standup.md")); // a folder not made yet
        QSignalSpy created(m_window.get(), &MainWindow::noteCreated);
        dialog->okButton()->click();
        QVERIFY(created.count() == 1 || created.wait(kWaitMs));
        const QString text = read(m_vault.filePath(QStringLiteral("meetings/Standup.md")));
        QCOMPARE(text, QStringLiteral("---\ntags: [meeting]\n---\n# Standup\n\nHeld %1.\n").arg(QDate::currentDate().toString(Qt::ISODate)));
    }

    void templateTakesARealIdInAProject()
    {
        QSignalSpy created(m_window.get(), &MainWindow::noteCreated);
        m_window->createNote(m_vault.filePath(QStringLiteral("proj/Why SQLite.md")), m_vault.filePath(QStringLiteral("templates/Decision note.md")));
        QVERIFY(created.count() == 1 || created.wait(kWaitMs));
        QCOMPARE(read(m_vault.filePath(QStringLiteral("proj/Why SQLite.md"))), QStringLiteral("# Why SQLite\n\nRecorded as D-002.\n"));
        // Outside a project, left as written.
        m_window->createNote(m_vault.filePath(QStringLiteral("Loose.md")), m_vault.filePath(QStringLiteral("templates/Decision note.md")));
        QVERIFY(created.count() == 2 || created.wait(kWaitMs));
        QCOMPARE(read(m_vault.filePath(QStringLiteral("Loose.md"))), QStringLiteral("# Loose\n\nRecorded as {{next:D}}.\n"));
    }

    void anExistingNoteIsNeverOverwritten()
    {
        const QString path = m_vault.filePath(QStringLiteral("proj/notes/today.md"));
        QSignalSpy created(m_window.get(), &MainWindow::noteCreated);
        m_window->createNote(path, QString());
        QCOMPARE(created.count(), 0);
        QCOMPARE(read(path), QStringLiteral("Notes.\n"));
    }

    // D-025: Edit Property Values makes the note once and opens it; a saved
    // change to it reaches the Properties card of a note already open.
    void editPropertyValuesAndTheCardFollows()
    {
        const QString scene = m_vault.filePath(QStringLiteral("prose/scene.md"));
        write(scene, "---\nstatus: draft\n---\nA scene.\n");
        m_window->openNote(scene);
        PropertiesPanel* card = m_window->currentView()->properties();
        QCOMPARE(card->errorCount(), 0); // nothing declared: status is the author's own

        QAction* action = actionNamed(*m_window, QStringLiteral("Edit Property Values"));
        QVERIFY(action);
        action->trigger();
        const QString values = m_vault.filePath(QStringLiteral("Coeus properties.md"));
        QVERIFY(QFile::exists(values));
        QCOMPARE(m_window->currentEditor()->path(), values);
        QVERIFY(m_window->currentEditor()->toPlainText().startsWith(QStringLiteral("# Coeus properties")));
        // Again: opened, never rewritten.
        write(values, "## status\nFolders: prose\n- outline\n- revised\n");
        m_window->openNote(scene);
        action->trigger();
        QTRY_COMPARE(m_window->currentEditor()->toPlainText(), QStringLiteral("## status\nFolders: prose\n- outline\n- revised\n"));
        // The scene's card follows the new list once the change is seen.
        QTRY_COMPARE_WITH_TIMEOUT(card->errorCount(), 1, kWaitMs);
        QVERIFY(qobject_cast<QComboBox*>(card->field(QStringLiteral("status"))));
    }

    void cleanupTestCase()
    {
        m_window.reset();
    }
};

QTEST_MAIN(TestNewNote)
#include "test_new_note.moc"
