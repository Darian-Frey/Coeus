// F-015 in the window: Insert Next ID, {{next:KIND}} in templates, ID
// completion and the register panel, over a real index.

#include "index/Database.hpp"
#include "index/IndexService.hpp"
#include "ui/CommandPalette.hpp"
#include "ui/Editor.hpp"
#include "ui/MainWindow.hpp"
#include "ui/NoteCatalogue.hpp"
#include "ui/RegisterPanel.hpp"
#include "ui/WikilinkCompleter.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QComboBox>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTreeWidget>

using namespace coeus;

namespace {

constexpr int kWaitMs = 5000;

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
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QAction* action(MainWindow& window, const QString& text)
{
    for (QAction* a : window.registeredActions()) {
        if (a->text().remove(QLatin1Char('&')) == text)
            return a;
    }
    return nullptr;
}

const QByteArray kHeader = "> **Status:** Active\n> **Provenance:** T\n> **Last reviewed:** 2026-10-13\n> **Why this status:** T\n\n# Proj\n";

} // namespace

class TestRegisterCommands : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_settings;
    QTemporaryDir m_vault;
    std::unique_ptr<MainWindow> m_window;

    bool catalogued(const QString& ident) const
    {
        const auto& entries = m_window->catalogue()->registers();
        return std::any_of(entries.begin(), entries.end(), [&](const RegisterRef& e) { return e.ident == ident; });
    }

private slots:
    void initTestCase()
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        write(m_vault.filePath(QStringLiteral("proj/README.md")), kHeader);
        write(m_vault.filePath(QStringLiteral("proj/DECISIONS.md")),
              "# Decisions\n\n### D-001 First\n**Status:** Accepted\n\n### D-002 Second\n**Status:** Proposed\n");
        write(m_vault.filePath(QStringLiteral("proj/FEATURES.md")), "### F-001 Shell\n**Status:** Complete\n");
        write(m_vault.filePath(QStringLiteral("proj/notes.md")), "Notes.\n");
        write(m_vault.filePath(QStringLiteral("templates/Decision.md")), "Logged {{next:D}} today.\n");
        write(m_vault.filePath(QStringLiteral("loose.md")), "Outside every project.\n");
        m_window = std::make_unique<MainWindow>();
        m_window->resize(1000, 700);
        m_window->show();
        QSignalSpy reloaded(m_window->catalogue(), &NoteCatalogue::reloaded);
        m_window->openVault(*Vault::open(m_vault.path()));
        QVERIFY(reloaded.wait(kWaitMs));
    }

    void insertNextIdAppendsAReservedEntry()
    {
        m_window->openNote(m_vault.filePath(QStringLiteral("proj/DECISIONS.md")));
        action(*m_window, QStringLiteral("Insert Next ID…"))->trigger();
        CommandPalette* picker = m_window->nextIdPicker();
        QVERIFY(picker->isVisible());
        QSignalSpy inserted(m_window.get(), &MainWindow::nextIdInserted);
        QTest::keyClick(picker->focusWidget(), Qt::Key_Return); // D first: we are in DECISIONS.md
        QVERIFY(inserted.wait(kWaitMs));
        QCOMPARE(inserted.front().at(0).toString(), QStringLiteral("D-003"));

        Editor* editor = m_window->currentEditor();
        QVERIFY(editor->toPlainText().contains(QStringLiteral("**Status:** Proposed\n\n### D-003 Title\n**Decided:** ")));
        QCOMPARE(editor->textCursor().selectedText(), QStringLiteral("Title")); // ready to type over
        QVERIFY(!read(m_vault.filePath(QStringLiteral("proj/DECISIONS.md"))).contains("D-003")); // nothing saved yet

        // Typed over and still unsaved, and its reservation lapsed (a long
        // pause): only the open buffer now holds D-003, and must be read.
        editor->insertPlainText(QStringLiteral("Third"));
        {
            auto db = Database::open(IndexService::indexPath(m_vault.path()));
            QVERIFY(db && db->exec("DELETE FROM reservations"));
        }
        action(*m_window, QStringLiteral("Insert Next ID…"))->trigger();
        QTest::keyClick(picker->focusWidget(), Qt::Key_Return);
        QVERIFY(inserted.wait(kWaitMs));
        QCOMPARE(inserted.back().at(0).toString(), QStringLiteral("D-004"));
        QVERIFY(m_window->saveCurrent());
    }

    void aMissingRegisterIsCreatedFromTheSkeleton()
    {
        m_window->openNote(m_vault.filePath(QStringLiteral("proj/notes.md")));
        action(*m_window, QStringLiteral("Insert Next ID…"))->trigger();
        CommandPalette* picker = m_window->nextIdPicker();
        picker->setFilterText(QStringLiteral("Improvements"));
        QSignalSpy inserted(m_window.get(), &MainWindow::nextIdInserted);
        QTest::keyClick(picker->focusWidget(), Qt::Key_Return);
        QVERIFY(inserted.wait(kWaitMs));
        QCOMPARE(inserted.front().at(0).toString(), QStringLiteral("IMP-001"));
        const QString file = m_vault.filePath(QStringLiteral("proj/IMPROVEMENTS.md"));
        QCOMPARE(inserted.front().at(1).toString(), file);
        QVERIFY(read(file).startsWith("# Improvements\n")); // the skeleton, written
        QVERIFY(m_window->currentEditor()->toPlainText().contains(QStringLiteral("### IMP-001: Title")));
        QVERIFY(m_window->saveCurrent());
    }

    void templatesTakeARealId()
    {
        QTRY_VERIFY_WITH_TIMEOUT(catalogued(QStringLiteral("D-004")), kWaitMs); // saved and indexed
        m_window->openNote(m_vault.filePath(QStringLiteral("proj/notes.md")));
        Editor* editor = m_window->currentEditor();
        editor->moveCursor(QTextCursor::End);
        QSignalSpy inserted(m_window.get(), &MainWindow::templateInserted);
        m_window->insertTemplate(m_vault.filePath(QStringLiteral("templates/Decision.md")));
        QVERIFY(inserted.wait(kWaitMs));
        QVERIFY(editor->toPlainText().endsWith(QStringLiteral("Logged D-005 today.\n")));
        editor->document()->setModified(false);
    }

    void idCompletionOffersTheProjectsEntries()
    {
        m_window->openNote(m_vault.filePath(QStringLiteral("proj/notes.md")));
        Editor* editor = m_window->currentEditor();
        editor->selectAll();
        editor->textCursor().removeSelectedText();
        editor->setFocus();
        QTest::keyClicks(editor, QStringLiteral("See D-"));
        QVERIFY(editor->completer()->isOpen());
        QVERIFY(editor->completer()->popup()->model()->rowCount() >= 4);
        QTest::keyClicks(editor, QStringLiteral("00"));
        QTest::keyClick(editor->completer()->popup(), Qt::Key_Return);
        QCOMPARE(editor->toPlainText(), QStringLiteral("See D-001"));
        editor->document()->setModified(false);
    }

    void registerPanelFollowsFiltersAndOpens()
    {
        QTRY_VERIFY_WITH_TIMEOUT(catalogued(QStringLiteral("IMP-001")), kWaitMs);
        m_window->openNote(m_vault.filePath(QStringLiteral("proj/FEATURES.md")));
        RegisterPanel* panel = m_window->registers();
        QCOMPARE(panel->projectBox()->currentText(), QStringLiteral("proj"));
        QVERIFY(panel->tree()->topLevelItemCount() >= 6); // D-001…D-004, F-001, IMP-001
        panel->kindBox()->setCurrentIndex(panel->kindBox()->findData(QStringLiteral("D")));
        QCOMPARE(panel->tree()->topLevelItem(0)->text(0), QStringLiteral("D-001"));
        panel->statusBox()->setCurrentIndex(panel->statusBox()->findData(QStringLiteral("Accepted")));
        QCOMPARE(panel->tree()->topLevelItemCount(), 1);
        emit panel->tree()->itemClicked(panel->tree()->topLevelItem(0), 0);
        QCOMPARE(m_window->currentEditor()->path(), m_vault.filePath(QStringLiteral("proj/DECISIONS.md")));
        QCOMPARE(m_window->currentEditor()->textCursor().block().text(), QStringLiteral("### D-001 First"));
    }

    void outsideAProjectThereIsNothingToNumber()
    {
        m_window->openNote(m_vault.filePath(QStringLiteral("loose.md")));
        m_window->nextIdPicker()->hide();
        action(*m_window, QStringLiteral("Insert Next ID…"))->trigger();
        QVERIFY(!m_window->nextIdPicker()->isVisible());
    }

    void cleanupTestCase()
    {
        for (int i = 0; i < m_window->tabs()->count(); ++i)
            m_window->editorAt(i)->document()->setModified(false);
        m_window.reset();
    }
};

QTEST_MAIN(TestRegisterCommands)
#include "test_register_commands.moc"
