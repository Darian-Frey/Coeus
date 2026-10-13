// F-007 in the window: the tag pane lists tags with counts, a tag filters
// the explorer, Clear restores it, and reading-mode tag links do the same.

#include "ui/Editor.hpp"
#include "ui/ExplorerPanel.hpp"
#include "ui/MainWindow.hpp"
#include "ui/NoteView.hpp"
#include "ui/TagPanel.hpp"

#include <QFile>
#include <QLineEdit>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBrowser>
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

QStringList listed(ExplorerPanel* explorer)
{
    QStringList out;
    for (int i = 0; i < explorer->filterList()->topLevelItemCount(); ++i)
        out.append(explorer->filterList()->topLevelItem(i)->text(0));
    return out;
}

} // namespace

class TestTagPanel : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_settings;

private slots:
    void initTestCase()
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    }

    void listsFiltersAndClears()
    {
        QTemporaryDir vault;
        write(vault.filePath(QStringLiteral("a.md")), "#project/coeus and #idea\n");
        write(vault.filePath(QStringLiteral("notes/b.md")), "---\ntags: [project]\n---\n");
        write(vault.filePath(QStringLiteral("c.md")), "no tags\n");

        MainWindow window;
        QSignalSpy reloaded(window.tags(), &TagPanel::reloaded);
        window.openVault(*Vault::open(vault.path()));
        QTRY_VERIFY_WITH_TIMEOUT(window.tags()->tree()->topLevelItemCount() == 2, kWaitMs);

        QTreeWidget* tree = window.tags()->tree();
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("#idea"));
        QTreeWidgetItem* project = tree->topLevelItem(1);
        QCOMPARE(project->text(0), QStringLiteral("#project"));
        QCOMPARE(project->text(1), QStringLiteral("2"));
        QCOMPARE(project->child(0)->text(0), QStringLiteral("#coeus"));

        // Clicking a tag filters the explorer to its notes.
        emit tree->itemClicked(project, 0);
        ExplorerPanel* explorer = window.explorer();
        QVERIFY(explorer->isFiltered());
        QCOMPARE(explorer->filterLabel(), QStringLiteral("#project"));
        QCOMPARE(listed(explorer), (QStringList{QStringLiteral("a.md"), QStringLiteral("notes/b.md")}));

        // The filtered list follows the index.
        write(vault.filePath(QStringLiteral("d.md")), "#project too\n");
        QTRY_COMPARE_WITH_TIMEOUT(listed(explorer).size(), 3, kWaitMs);

        // Opening from the list, then Clear restores the folder tree.
        emit explorer->filterList()->itemClicked(explorer->filterList()->topLevelItem(0), 0);
        QCOMPARE(window.currentEditor()->path(), vault.filePath(QStringLiteral("a.md")));
        explorer->findChild<QToolButton*>(QStringLiteral("clearFilter"))->click();
        QVERIFY(!explorer->isFiltered());
        QVERIFY(explorer->view(0)->isVisibleTo(explorer));

        // The filter box narrows the tag list.
        window.tags()->filter()->setText(QStringLiteral("ide"));
        QCOMPARE(tree->topLevelItemCount(), 1);
        window.tags()->filter()->clear();
    }

    void readingModeTagLinksFilterToo()
    {
        QTemporaryDir vault;
        write(vault.filePath(QStringLiteral("a.md")), "See #topic here.\n");
        write(vault.filePath(QStringLiteral("b.md")), "#topic as well\n");
        MainWindow window;
        window.openVault(*Vault::open(vault.path()));
        QTRY_VERIFY_WITH_TIMEOUT(window.tags()->tree()->topLevelItemCount() == 1, kWaitMs);

        window.openNote(vault.filePath(QStringLiteral("a.md")));
        NoteView* view = window.currentView();
        view->setMode(NoteView::Mode::Reading);
        emit view->reader()->anchorClicked(QUrl(QStringLiteral("coeus-tag:topic")));
        QVERIFY(window.explorer()->isFiltered());
        QCOMPARE(listed(window.explorer()).size(), 2);
    }
};

QTEST_MAIN(TestTagPanel)
#include "test_tag_panel.moc"
