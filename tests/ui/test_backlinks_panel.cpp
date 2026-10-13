// F-005 in the window: the sidebar lists backlinks and unlinked mentions for
// the current tab, follows edits through the index, and opens the source at
// the linking line.

#include "ui/BacklinksPanel.hpp"
#include "ui/Editor.hpp"
#include "ui/MainWindow.hpp"

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

} // namespace

class TestBacklinksPanel : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_settings;

private slots:
    void initTestCase()
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    }

    void listsFollowsAndOpens()
    {
        QTemporaryDir vault;
        write(vault.filePath(QStringLiteral("Target.md")), "# Target\n");
        write(vault.filePath(QStringLiteral("source.md")), "one\ntwo\nLinks [[Target]] here.\n");
        write(vault.filePath(QStringLiteral("other.md")), "A plain Target mention.\n");

        MainWindow window;
        QSignalSpy opened(&window, &MainWindow::indexOpened);
        window.openVault(*Vault::open(vault.path()));
        QVERIFY(opened.wait(kWaitMs));

        BacklinksPanel* panel = window.backlinks();
        QSignalSpy updated(panel, &BacklinksPanel::updated);
        window.openNote(vault.filePath(QStringLiteral("Target.md")));
        QTRY_VERIFY_WITH_TIMEOUT(!updated.isEmpty() && updated.last().at(0).toInt() == 1, kWaitMs);
        QCOMPARE(updated.last().at(1).toInt(), 1);
        QCOMPARE(panel->linksTree()->topLevelItem(0)->text(0), QStringLiteral("source.md"));
        QCOMPARE(panel->linksTree()->topLevelItem(0)->child(0)->text(0), QStringLiteral("Links [[Target]] here."));
        QCOMPARE(panel->mentionsTree()->topLevelItem(0)->text(0), QStringLiteral("other.md"));

        // A new link elsewhere appears once the index has it.
        write(vault.filePath(QStringLiteral("late.md")), "[[Target]]\n");
        QTRY_VERIFY_WITH_TIMEOUT(updated.last().at(0).toInt() == 2, kWaitMs);

        // Clicking a backlink opens the source at its line.
        QTreeWidgetItem* line = panel->linksTree()->findItems(QStringLiteral("source.md"), Qt::MatchExactly).front()->child(0);
        emit panel->linksTree()->itemClicked(line, 0);
        Editor* editor = window.currentEditor();
        QCOMPARE(editor->path(), vault.filePath(QStringLiteral("source.md")));
        QCOMPARE(editor->textCursor().blockNumber(), 2); // line 3
    }

    void emptyStateWithoutANote()
    {
        QTemporaryDir vault;
        write(vault.filePath(QStringLiteral("a.md")), "x\n");
        MainWindow window;
        window.openVault(*Vault::open(vault.path()));
        QCOMPARE(window.backlinks()->linksTree()->topLevelItemCount(), 0);
    }
};

QTEST_MAIN(TestBacklinksPanel)
#include "test_backlinks_panel.moc"
