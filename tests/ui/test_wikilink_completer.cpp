// F-004: typing [[ in the editor offers notes, aliases and headings from the
// index, and the chosen link resolves to the chosen note.

#include "ui/Editor.hpp"
#include "ui/MainWindow.hpp"
#include "ui/NoteCatalogue.hpp"
#include "ui/WikilinkCompleter.hpp"

#include <QAbstractItemView>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
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

QStringList rows(QAbstractItemView* view)
{
    QStringList out;
    for (int r = 0; r < view->model()->rowCount(); ++r)
        out.append(view->model()->index(r, 0).data().toString());
    return out;
}

} // namespace

class TestWikilinkCompleter : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_settings;
    QTemporaryDir m_vault;
    std::unique_ptr<MainWindow> m_window;
    Editor* m_editor = nullptr;

    void accept() { QTest::keyClick(m_editor->completer()->popup(), Qt::Key_Return); }

private slots:
    void initTestCase()
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());

        write(m_vault.filePath(QStringLiteral("Alpha.md")), "# Alpha\n\n## Context\n\n## Consequences\n");
        write(m_vault.filePath(QStringLiteral("Coeus.md")), "---\naliases: [The Pole]\n---\n");
        write(m_vault.filePath(QStringLiteral("projects/coeus/README.md")), "x");
        write(m_vault.filePath(QStringLiteral("projects/pontus/README.md")), "x");
        write(m_vault.filePath(QStringLiteral("projects/coeus/scratch.md")), "");

        m_window = std::make_unique<MainWindow>();
        QSignalSpy reloaded(m_window->catalogue(), &NoteCatalogue::reloaded);
        m_window->show();
        m_window->openVault(*Vault::open(m_vault.path()));
        QVERIFY(reloaded.wait(5000));
        m_window->openNote(m_vault.filePath(QStringLiteral("projects/coeus/scratch.md")));
        m_editor = m_window->currentEditor();
        QVERIFY(m_editor->completer());
        m_editor->setFocus();
    }

    void init()
    {
        m_editor->selectAll();
        m_editor->textCursor().removeSelectedText();
    }

    void opensOnDoubleBracketAndInsertsTitle()
    {
        QTest::keyClicks(m_editor, QStringLiteral("See [[alph"));
        QVERIFY(m_editor->completer()->isOpen());
        QVERIFY(rows(m_editor->completer()->popup()).front().startsWith(QStringLiteral("Alpha")));
        accept();
        QCOMPARE(m_editor->toPlainText(), QStringLiteral("See [[Alpha]]"));
        QVERIFY(!m_editor->completer()->isOpen());
    }

    void sharedTitleInsertsAPathThatResolvesToTheChoice()
    {
        QTest::keyClicks(m_editor, QStringLiteral("[[READ"));
        const QStringList shown = rows(m_editor->completer()->popup());
        QCOMPARE(shown.size(), 2);
        QVERIFY(shown.front().contains(QStringLiteral("projects/coeus"))); // nearest folder first
        accept();
        QCOMPARE(m_editor->toPlainText(), QStringLiteral("[[projects/coeus/README]]"));
    }

    void aliasInsertsTitleAndAlias()
    {
        QTest::keyClicks(m_editor, QStringLiteral("[[pole"));
        accept();
        QCOMPARE(m_editor->toPlainText(), QStringLiteral("[[Coeus|The Pole]]"));
    }

    void headingsAfterHash()
    {
        QTest::keyClicks(m_editor, QStringLiteral("[[Alpha#con"));
        QCOMPARE(rows(m_editor->completer()->popup()).size(), 2);
        accept();
        QCOMPARE(m_editor->toPlainText(), QStringLiteral("[[Alpha#Context]]"));
    }

    void existingClosingBracketsAreReused()
    {
        m_editor->insertPlainText(QStringLiteral("[[]] after"));
        QTextCursor cursor = m_editor->textCursor();
        cursor.setPosition(2);
        m_editor->setTextCursor(cursor);
        QTest::keyClicks(m_editor, QStringLiteral("alp"));
        accept();
        QCOMPARE(m_editor->toPlainText(), QStringLiteral("[[Alpha]] after"));
        QCOMPARE(m_editor->textCursor().position(), 9); // after the ]]
    }

    void escapeAndClosedLinksDismiss()
    {
        QTest::keyClicks(m_editor, QStringLiteral("[["));
        QVERIFY(m_editor->completer()->isOpen());
        QTest::keyClick(m_editor->completer()->popup(), Qt::Key_Escape);
        QVERIFY(!m_editor->completer()->isOpen());
        QCOMPARE(m_editor->toPlainText(), QStringLiteral("[["));

        QTest::keyClicks(m_editor, QStringLiteral("Alpha]] more"));
        QVERIFY(!m_editor->completer()->isOpen());
    }

    void cleanupTestCase()
    {
        m_editor->document()->setModified(false);
        m_window.reset();
    }
};

QTEST_MAIN(TestWikilinkCompleter)
#include "test_wikilink_completer.moc"
