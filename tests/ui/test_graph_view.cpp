// F-026 in the window: the global graph as a tab, the local graph in the
// sidebar, clicks opening notes and filtering by tags, and the choices.

#include "ui/Editor.hpp"
#include "ui/ExplorerPanel.hpp"
#include "ui/GraphPanel.hpp"
#include "ui/GraphView.hpp"
#include "ui/MainWindow.hpp"
#include "ui/Workspace.hpp"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>

using namespace coeus;

namespace {

void write(const QString& path, const QByteArray& content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write(content);
}

QAction* actionNamed(MainWindow& window, const QString& text)
{
    for (QAction* action : window.registeredActions())
        if (action->text().remove(QLatin1Char('&')) == text)
            return action;
    return nullptr;
}

const QByteArray kHeader = "> **Status:** Active\n> **Provenance:** T\n> **Last reviewed:** 2026-10-14\n"
                           "> **Why this status:** T\n\n# Alpha\n\nSee [[a]].\n";

// Clicks a node where it is drawn.
void click(GraphView* view, int node)
{
    const QPoint at = view->screenPosition(node).toPoint();
    QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, at);
    QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, at);
}

} // namespace

class TestGraphView : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_settings;
    QTemporaryDir m_vault;

    QString note(const char* path) const { return m_vault.filePath(QLatin1String(path)); }

private slots:
    void initTestCase()
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        write(note("a.md"), "# A\n\n[[b]] and [[Future idea]].\n\n#area/x\n");
        write(note("b.md"), "# B\n\n[[c]].\n");
        write(note("c.md"), "# C\n\n#area\n");
        write(note("d.md"), "# D\n\nAlone.\n");
        write(note("projects/alpha/README.md"), kHeader);
    }

    void globalGraphTabAndLocalGraph()
    {
        MainWindow window;
        window.resize(1200, 800);
        window.show();
        QSignalSpy opened(&window, &MainWindow::indexOpened);
        window.openVault(*Vault::open(m_vault.path()));
        QVERIFY(opened.wait(5000));

        // The local graph follows the current note.
        GraphPanel* local = window.localGraph();
        QVERIFY(local);
        QSignalSpy localUpdated(local, &GraphPanel::updated);
        window.openNote(note("a.md"));
        QTRY_VERIFY(!localUpdated.isEmpty());
        QCOMPARE(local->view()->graph().nodes().front().path, note("a.md")); // a at the centre
        QCOMPARE(local->view()->graph().nodes().size(), 4);                  // a, b, README, #area/x
        QCOMPARE(local->status(), QStringLiteral("3 note(s) within reach"));
        localUpdated.clear();
        local->depthBox()->setValue(2);
        QTRY_VERIFY(!localUpdated.isEmpty());
        QVERIFY(local->view()->graph().indexOf(QStringLiteral("n:vault/c.md")) >= 0);
        QCOMPARE(QSettings().value(QStringLiteral("graph/local/depth")).toInt(), 2);
        local->depthBox()->setValue(1);

        // Open Graph View: one tab, the whole vault.
        QAction* open = actionNamed(window, QStringLiteral("Open Graph View"));
        QVERIFY(open);
        QCOMPARE(open->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_G));
        open->trigger();
        GraphPanel* global = window.graphTab();
        QVERIFY(global);
        QCOMPARE(window.tabs()->currentWidget(), global);
        QSignalSpy updated(global, &GraphPanel::updated);
        QTRY_VERIFY(!updated.isEmpty());
        QCOMPARE(global->status(), QStringLiteral("4 note(s) · 5 connection(s)"));
        open->trigger(); // again: the same tab
        QCOMPARE(window.tabs()->count(), 2);
        GraphView* view = global->view();
        view->settle();
        QVERIFY(view->settled());
        // Coloured by project: README's project in the legend; notes outside one are grey.
        QCOMPARE(view->legend(), QStringList{QStringLiteral("alpha")});
        const int readme = view->graph().indexOf(QStringLiteral("n:vault/projects/alpha/README.md"));
        QVERIFY(view->nodeColour(readme) != view->nodeColour(view->graph().indexOf(QStringLiteral("n:vault/b.md"))));
        // By tag: a and c share #area's colour.
        global->colouring()->setCurrentIndex(1);
        QCOMPARE(view->colouring(), GraphView::Colouring::Tag);
        QCOMPARE(view->legend().front(), QStringLiteral("area"));
        QCOMPARE(view->nodeColour(view->graph().indexOf(QStringLiteral("n:vault/a.md"))),
                 view->nodeColour(view->graph().indexOf(QStringLiteral("n:vault/c.md"))));

        // A click on a note opens it; on a tag, filters the explorer.
        click(view, view->graph().indexOf(QStringLiteral("n:vault/b.md")));
        QCOMPARE(window.currentEditor()->path(), note("b.md"));
        QTRY_COMPARE(local->view()->graph().nodes().front().path, note("b.md"));
        window.tabs()->setCurrentWidget(global);
        view->settle();
        click(view, view->graph().indexOf(QStringLiteral("t:area")));
        QVERIFY(window.explorer()->isFiltered());
        QCOMPARE(window.explorer()->filterLabel(), QStringLiteral("#area"));

        // The choices: orphans and unresolved links shown on request, and kept.
        updated.clear();
        global->orphansBox()->setChecked(true);
        QTRY_VERIFY(!updated.isEmpty());
        QVERIFY(view->graph().indexOf(QStringLiteral("n:vault/d.md")) >= 0);
        updated.clear();
        global->unresolvedBox()->setChecked(true);
        QTRY_VERIFY(!updated.isEmpty());
        QVERIFY(view->graph().indexOf(QStringLiteral("g:future idea")) >= 0);
        QVERIFY(QSettings().value(QStringLiteral("graph/global/orphans")).toBool());
        QCOMPARE(QSettings().value(QStringLiteral("graph/global/colouring")).toInt(), 1);

        // An edit that adds a link reaches the graph once indexed.
        updated.clear();
        write(note("d.md"), "# D\n\nNow linked to [[c]].\n");
        QTRY_VERIFY_WITH_TIMEOUT(!updated.isEmpty() && view->graph().nodes()[view->graph().indexOf(QStringLiteral("n:vault/d.md"))].degree == 1,
                                 10000);

        // The tab closes like any other.
        emit window.tabs()->tabCloseRequested(window.tabs()->indexOf(global));
        QTRY_VERIFY(!window.graphTab());
    }
};

QTEST_MAIN(TestGraphView)
#include "test_graph_view.moc"
