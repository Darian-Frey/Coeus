// F-016 in the window: the Problems dock, its status-bar count, and lint
// underlines in the editor, over a real index.

#include "ui/Editor.hpp"
#include "ui/MainWindow.hpp"
#include "ui/NoteCatalogue.hpp"
#include "ui/ProblemsPanel.hpp"

#include <QAction>
#include <QClipboard>
#include <QGuiApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QToolButton>
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

QByteArray header(const char* reviewed)
{
    return QByteArray("> **Status:** Active\n> **Provenance:** T\n> **Last reviewed:** ") + reviewed
           + "\n> **Why this status:** T\n\n# Project\n";
}

int problemCount(const QTreeWidget* tree)
{
    int count = 0;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        count += tree->topLevelItem(i)->childCount();
    return count;
}

} // namespace

class TestProblemsPanel : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_settings;
    QTemporaryDir m_vault;
    std::unique_ptr<MainWindow> m_window;

    bool catalogued(const char* subject) const
    {
        const auto& problems = m_window->catalogue()->problems();
        return std::any_of(problems.begin(), problems.end(),
                           [subject](const ProblemRef& p) { return p.subject == QLatin1String(subject); });
    }

private slots:
    void initTestCase()
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        write(m_vault.filePath(QStringLiteral("p/README.md")), header(QDate::currentDate().toString(Qt::ISODate).toLatin1()));
        write(m_vault.filePath(QStringLiteral("p/DECISIONS.md")),
              "# Decisions\n\n"
              "### D-001 First\n"                     // 3
              "**Status:** Accepted\n"                // 4
              "**Related:** AV-001, F-404\n\n"        // 5
              "**Reversal conditions.** None.\n");
        write(m_vault.filePath(QStringLiteral("p/ATTACK_VECTORS.md")), "### AV-001 Drift\n**Severity:** Major\n");
        write(m_vault.filePath(QStringLiteral("q/README.md")), header("2020-01-01")); // stale review: info
        write(m_vault.filePath(QStringLiteral("q/BUGS.md")), "### BUG-001: Q's bug\n**Status:** closed\n");
        write(m_vault.filePath(QStringLiteral("loose.md")), "Outside every project.\n");
        m_window = std::make_unique<MainWindow>();
        m_window->resize(1100, 750);
        m_window->show();
        QSignalSpy reloaded(m_window->catalogue(), &NoteCatalogue::reloaded);
        m_window->openVault(*Vault::open(m_vault.path()));
        QVERIFY(reloaded.wait(kWaitMs));
        QTRY_VERIFY_WITH_TIMEOUT(catalogued("F-404"), kWaitMs);
    }

    void statusBarCountsTheCurrentProject()
    {
        m_window->openNote(m_vault.filePath(QStringLiteral("loose.md")));
        QVERIFY(!m_window->problemsButton()->isVisible());
        m_window->openNote(m_vault.filePath(QStringLiteral("p/DECISIONS.md")));
        QVERIFY(m_window->problemsButton()->isVisible());
        // F-404 unresolved (L1), AV-001 without Detection (L7); D-001 → AV-001 not returned (L4).
        QCOMPARE(m_window->problemsButton()->text(), QStringLiteral("✖ 2  ⚠ 1"));
        QVERIFY(!m_window->problemsDock()->isVisible());
        m_window->problemsButton()->click();
        QVERIFY(m_window->problemsDock()->isVisible());
    }

    void panelFiltersByProjectAndLevel()
    {
        ProblemsPanel* panel = m_window->problems();
        QTreeWidget* tree = panel->tree();
        QCOMPARE(tree->topLevelItemCount(), 2); // ATTACK_VECTORS.md, DECISIONS.md
        QCOMPARE(problemCount(tree), 3);
        QCOMPARE(panel->summary(), QStringLiteral("2 errors · 1 warning · 0 info"));
        panel->levelBox()->setCurrentIndex(panel->levelBox()->findText(QStringLiteral("Errors")));
        QCOMPARE(problemCount(tree), 2);
        panel->scopeBox()->setCurrentIndex(panel->scopeBox()->findText(QStringLiteral("All projects")));
        QCOMPARE(problemCount(tree), 3); // q's "closed" is not a BUG status (L3)
        panel->levelBox()->setCurrentIndex(panel->levelBox()->findText(QStringLiteral("All levels")));
        QCOMPARE(problemCount(tree), 5); // and q's review is years old (L8)
        panel->scopeBox()->setCurrentIndex(panel->scopeBox()->findText(QStringLiteral("This project")));
    }

    void copyReportForAnAiSession()
    {
        ProblemsPanel* panel = m_window->problems();
        QSignalSpy copied(panel, &ProblemsPanel::reportCopied);
        panel->copyButton()->click();
        QCOMPARE(copied.count(), 1);
        QCOMPARE(copied.front().front().toInt(), 3);
        const QString report = panel->report();
        QCOMPARE(QGuiApplication::clipboard()->text(), report);
        QVERIFY(report.startsWith(QStringLiteral("# Register problems: p\n\n%1 · 2 error(s) · 1 warning(s) · 0 info")
                                      .arg(QDate::currentDate().toString(Qt::ISODate))));
        QVERIFY(report.contains(QLatin1String("\n## DECISIONS.md\n\n- `DECISIONS.md:5` error L1: ")));
        QVERIFY(report.contains(QLatin1String("\n## ATTACK_VECTORS.md\n\n- `ATTACK_VECTORS.md:1` error L7: ")));
        QVERIFY(report.contains(QLatin1String("\n## Rules\n\n- L1: a Related field cites an ID the project does not define.\n")));
        QVERIFY(!report.contains(QLatin1String("q/"))); // another project's problems

        // What the panel shows: every project, errors only, paths from the root.
        panel->scopeBox()->setCurrentIndex(panel->scopeBox()->findText(QStringLiteral("All projects")));
        panel->levelBox()->setCurrentIndex(panel->levelBox()->findText(QStringLiteral("Errors")));
        const QString all = panel->report();
        QVERIFY(all.startsWith(QLatin1String("# Register problems: all projects (errors)\n")));
        QVERIFY(all.contains(QLatin1String("- `q/BUGS.md:2` error L3: ")));
        QVERIFY(all.contains(QLatin1String("- `p/DECISIONS.md:5` error L1: ")));
        QVERIFY(!all.contains(QLatin1String(" warning ")));
        panel->levelBox()->setCurrentIndex(panel->levelBox()->findText(QStringLiteral("All levels")));
        panel->scopeBox()->setCurrentIndex(panel->scopeBox()->findText(QStringLiteral("This project")));
    }

    void activatingAProblemOpensItsLine()
    {
        m_window->openNote(m_vault.filePath(QStringLiteral("loose.md")));
        QTreeWidget* tree = m_window->problems()->tree();
        QTreeWidgetItem* decisions = tree->topLevelItem(1);
        QVERIFY(decisions->text(0).startsWith(QStringLiteral("p/DECISIONS.md")));
        emit tree->itemClicked(decisions->child(0), 0);
        Editor* editor = m_window->currentEditor();
        QCOMPARE(editor->path(), m_vault.filePath(QStringLiteral("p/DECISIONS.md")));
        QCOMPARE(editor->textCursor().blockNumber() + 1, 5);
    }

    void editorUnderlinesProblems()
    {
        Editor* editor = m_window->currentEditor();
        const QTextBlock related = editor->document()->findBlockByNumber(4);
        const int at = related.position() + int(related.text().indexOf(QStringLiteral("F-404")));
        QVERIFY(editor->problemAt(at + 1).contains(QStringLiteral("F-404, which does not exist")));
        QVERIFY(editor->problemAt(related.position()).isEmpty()); // "**Related:**" itself is fine
        const auto selections = editor->extraSelections();
        QCOMPARE(selections.size(), 2); // AV-001 (warning) and F-404 (error) on line 5
        for (const auto& selection : selections)
            QCOMPARE(selection.format.underlineStyle(), QTextCharFormat::WaveUnderline);

        // Fix it and save: once indexed, the underline goes.
        QTextCursor cursor(related);
        cursor.setPosition(at);
        cursor.setPosition(at + 5, QTextCursor::KeepAnchor);
        editor->setTextCursor(cursor);
        editor->insertPlainText(QStringLiteral("AV-001"));
        QVERIFY(m_window->saveCurrent());
        QTRY_VERIFY_WITH_TIMEOUT(!catalogued("F-404"), kWaitMs);
        QTRY_VERIFY_WITH_TIMEOUT(editor->problemAt(at + 1).isEmpty(), kWaitMs);
        QCOMPARE(m_window->problemsButton()->text(), QStringLiteral("✖ 1  ⚠ 1"));
    }

    void toggledFromTheKeyboard()
    {
        QAction* toggle = m_window->problemsDock()->toggleViewAction();
        QCOMPARE(toggle->shortcut(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M));
        QVERIFY(m_window->registeredActions().contains(toggle));
    }

    void cleanupTestCase()
    {
        m_window.reset();
    }
};

QTEST_MAIN(TestProblemsPanel)
#include "test_problems_panel.moc"
