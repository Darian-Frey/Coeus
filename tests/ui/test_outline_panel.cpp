// F-013: the outline lists the current note's headings as a tree, follows
// unsaved edits, and goes to a heading in source or reading mode.

#include "ui/Editor.hpp"
#include "ui/MainWindow.hpp"
#include "ui/NoteView.hpp"
#include "ui/OutlinePanel.hpp"

#include <QFile>
#include <QScrollBar>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTreeWidget>

using namespace coeus;

namespace {

constexpr int kWaitMs = 3000;

void write(const QString& path, const QByteArray& content)
{
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write(content);
}

QString filler(int lines)
{
    QString text;
    for (int i = 0; i < lines; ++i)
        text += QStringLiteral("Paragraph line %1.\n\n").arg(i);
    return text;
}

} // namespace

class TestOutlinePanel : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_settings;
    QTemporaryDir m_vault;
    std::unique_ptr<MainWindow> m_window;
    OutlinePanel* m_outline = nullptr;

    // Opens a note and waits for its outline.
    void open(const QString& name, const QString& content)
    {
        write(m_vault.filePath(name), content.toUtf8());
        QSignalSpy updated(m_outline, &OutlinePanel::updated);
        m_window->openNote(m_vault.filePath(name));
        QVERIFY(updated.wait(kWaitMs) || !updated.isEmpty());
    }

private slots:
    void initTestCase()
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        m_window = std::make_unique<MainWindow>();
        m_window->resize(1000, 600);
        m_window->show();
        m_window->openVault(*Vault::open(m_vault.path()));
        m_outline = m_window->outline();
    }

    void nestsHeadingsByLevel()
    {
        open(QStringLiteral("nest.md"), QStringLiteral("---\ntitle: x\n---\n# One\n## One.A\n### One.A.i\n## One.B\n# Two\n#### Deep under Two\n"));
        QTreeWidget* tree = m_outline->tree();
        QCOMPARE(tree->topLevelItemCount(), 2);
        QTreeWidgetItem* one = tree->topLevelItem(0);
        QCOMPARE(one->text(0), QStringLiteral("One"));
        QCOMPARE(one->childCount(), 2);
        QCOMPARE(one->child(0)->child(0)->text(0), QStringLiteral("One.A.i"));
        QCOMPARE(tree->topLevelItem(1)->child(0)->text(0), QStringLiteral("Deep under Two")); // a skipped level still nests
        QCOMPARE(m_outline->headings().front().line, 4); // after the frontmatter
    }

    void followsUnsavedEdits()
    {
        open(QStringLiteral("live.md"), QStringLiteral("# First\n"));
        QSignalSpy updated(m_outline, &OutlinePanel::updated);
        Editor* editor = m_window->currentEditor();
        editor->moveCursor(QTextCursor::End);
        editor->insertPlainText(QStringLiteral("\n## Added later\n"));
        QVERIFY(updated.wait(kWaitMs));
        QCOMPARE(updated.last().front().toInt(), 2);
        QCOMPARE(m_outline->tree()->topLevelItem(0)->child(0)->text(0), QStringLiteral("Added later"));
        editor->document()->setModified(false);
    }

    void clickGoesToTheHeadingInSourceMode()
    {
        open(QStringLiteral("long.md"), QStringLiteral("# Top\n") + filler(80) + QStringLiteral("## Far down\n") + filler(80));
        QTreeWidgetItem* far = m_outline->tree()->topLevelItem(0)->child(0);
        emit m_outline->tree()->itemClicked(far, 0);
        Editor* editor = m_window->currentEditor();
        QCOMPARE(editor->textCursor().block().text(), QStringLiteral("## Far down"));
        QVERIFY(editor->verticalScrollBar()->value() > 100);
    }

    void clickGoesToTheHeadingInReadingMode()
    {
        open(QStringLiteral("read.md"), QStringLiteral("# Top\n") + filler(80) + QStringLiteral("## Repeated\n") + filler(40)
                                             + QStringLiteral("## Repeated\n") + filler(40));
        NoteView* view = m_window->currentView();
        view->setMode(NoteView::Mode::Reading);
        QScrollBar* scroll = view->reader()->verticalScrollBar();

        emit m_outline->tree()->itemClicked(m_outline->tree()->topLevelItem(0)->child(0), 0);
        const int first = scroll->value();
        QVERIFY(first > 0);
        emit m_outline->tree()->itemClicked(m_outline->tree()->topLevelItem(0)->child(1), 0);
        QVERIFY(scroll->value() > first); // the second "Repeated", not the first again
        view->setMode(NoteView::Mode::Source);
    }

    void highlightsTheSectionUnderTheCursor()
    {
        open(QStringLiteral("cursor.md"), QStringLiteral("# A\ntext\n## B\ntext\n"));
        Editor* editor = m_window->currentEditor();
        QTextCursor cursor(editor->document()->findBlockByNumber(3)); // "text" under B
        editor->setTextCursor(cursor);
        QCOMPARE(m_outline->tree()->currentItem()->text(0), QStringLiteral("B"));
        editor->setTextCursor(QTextCursor(editor->document()->findBlockByNumber(1)));
        QCOMPARE(m_outline->tree()->currentItem()->text(0), QStringLiteral("A"));
    }

    void emptyStates()
    {
        open(QStringLiteral("plain.md"), QStringLiteral("No headings at all.\n"));
        QVERIFY(m_outline->tree()->isHidden());
        m_outline->setView(nullptr);
        QCOMPARE(m_outline->headings().size(), 0);
    }

    void cleanupTestCase()
    {
        m_window.reset();
    }
};

QTEST_MAIN(TestOutlinePanel)
#include "test_outline_panel.moc"
