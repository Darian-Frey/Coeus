#include "ui/Editor.hpp"
#include "ui/MarkdownHighlighter.hpp"
#include "index/ContentHash.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextLayout>

using namespace coeus;

namespace {

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

// The highlighter format covering column `column` of line `line`.
QTextCharFormat formatAt(Editor& editor, int line, int column)
{
    const QTextBlock block = editor.document()->findBlockByNumber(line);
    for (const QTextLayout::FormatRange& range : block.layout()->formats()) {
        if (column >= range.start && column < range.start + range.length)
            return range.format;
    }
    return {};
}

} // namespace

class TestEditor : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    std::optional<Vault> m_vault;

    QString note(const QString& name, const QByteArray& content)
    {
        const QString path = m_dir.filePath(name);
        write(path, content);
        return path;
    }

private slots:
    void initTestCase()
    {
        m_vault = *Vault::open(m_dir.path());
    }

    void loadEditSave()
    {
        const QString path = note(QStringLiteral("a.md"), "# A\n");
        Editor editor(*m_vault, path);
        QVERIFY(editor.load().has_value());
        QVERIFY(!editor.isModified());
        QVERIFY(!editor.isReadOnly());

        editor.appendPlainText(QStringLiteral("more"));
        QVERIFY(editor.isModified());
        QCOMPARE(editor.save(), Editor::SaveResult::Saved);
        QCOMPARE(read(path), QByteArray("# A\n\nmore"));
        QVERIFY(!editor.isModified());
        QCOMPARE(editor.baseHash(), ContentHash::of(read(path)));
    }

    void saveNeverOverwritesAnExternalChange()
    {
        // AV-002: the buffer was based on "v1"; someone wrote "v2" meanwhile.
        const QString path = note(QStringLiteral("b.md"), "v1");
        Editor editor(*m_vault, path);
        QVERIFY(editor.load().has_value());
        editor.setPlainText(QStringLiteral("mine"));
        write(path, "v2");

        QCOMPARE(editor.save(), Editor::SaveResult::Conflict);
        QVERIFY(editor.inConflict());
        QCOMPARE(read(path), QByteArray("v2")); // untouched

        editor.keepMine();
        QCOMPARE(editor.save(), Editor::SaveResult::Saved);
        QCOMPARE(read(path), QByteArray("mine"));
        QVERIFY(!editor.inConflict());
    }

    void checkDiskReloadsOrConflicts()
    {
        const QString path = note(QStringLiteral("c.md"), "one\ntwo\nthree\n");
        Editor editor(*m_vault, path);
        QVERIFY(editor.load().has_value());
        QCOMPARE(editor.checkDisk(), Editor::DiskState::Unchanged);

        // Unmodified buffer: reloads, keeping the cursor where it was.
        QTextCursor cursor = editor.textCursor();
        cursor.setPosition(5);
        editor.setTextCursor(cursor);
        write(path, "one\nTWO\nthree\n");
        QCOMPARE(editor.checkDisk(), Editor::DiskState::Reloaded);
        QCOMPARE(editor.toPlainText(), QStringLiteral("one\nTWO\nthree\n"));
        QCOMPARE(editor.textCursor().position(), 5);
        QVERIFY(!editor.isModified());

        // Our own save comes back through the watcher: not a change.
        editor.insertPlainText(QStringLiteral("x"));
        QCOMPARE(editor.save(), Editor::SaveResult::Saved);
        QCOMPARE(editor.checkDisk(), Editor::DiskState::Unchanged);

        // Modified buffer: a conflict, and the takeDisk resolution.
        editor.insertPlainText(QStringLiteral("y"));
        write(path, "external\n");
        QCOMPARE(editor.checkDisk(), Editor::DiskState::Conflict);
        QVERIFY(editor.takeDisk().has_value());
        QCOMPARE(editor.toPlainText(), QStringLiteral("external\n"));
        QVERIFY(!editor.inConflict());
    }

    void deletedNoteKeepsTextAndSaveRecreatesIt()
    {
        const QString path = note(QStringLiteral("d.md"), "keep me");
        Editor editor(*m_vault, path);
        QVERIFY(editor.load().has_value());
        QVERIFY(QFile::remove(path));
        QCOMPARE(editor.checkDisk(), Editor::DiskState::Deleted);
        QVERIFY(editor.isModified());
        QCOMPARE(editor.save(), Editor::SaveResult::Saved);
        QCOMPARE(read(path), QByteArray("keep me"));
    }

    void restoreMarksRecoveredTextUnsaved()
    {
        const QString path = note(QStringLiteral("e.md"), "disk");
        Editor editor(*m_vault, path);
        QVERIFY(editor.load().has_value());
        editor.restore(QStringLiteral("recovered"), ContentHash::of("disk"));
        QVERIFY(editor.isModified());
        QCOMPARE(editor.save(), Editor::SaveResult::Saved);

        // Recovered on top of a version that has since changed: conflict.
        Editor stale(*m_vault, path);
        QVERIFY(stale.load().has_value());
        stale.restore(QStringLiteral("old work"), ContentHash::of("an older version"));
        QCOMPARE(stale.save(), Editor::SaveResult::Conflict);
    }

    void writeNoteRefusesPathsOutsideTheRoot()
    {
        QTemporaryDir other;
        QVERIFY(!m_vault->writeNote(other.filePath(QStringLiteral("x.md")), "x").has_value());
        QVERIFY(m_vault->writeNote(m_dir.filePath(QStringLiteral("new.md")), "x").has_value());
    }

    void highlightsMarkdown()
    {
        const QString path = note(QStringLiteral("h.md"),
                                  "---\ntags: [a]\n---\n"       // 0-2 frontmatter
                                  "# Title #tag\n"              // 3
                                  "See [[Link]] and #42 and #x2 `#code [[no]]`\n" // 4
                                  "```\n#notatag [[nolink]]\n```\n" // 5-7
                                  "Paragraph ^block-1\n");      // 8
        Editor editor(*m_vault, path);
        QVERIFY(editor.load().has_value());
        const MarkdownHighlighter* h = editor.highlighter();
        const QColor tag = h->format(MarkdownHighlighter::Kind::Tag).foreground().color();
        const QColor link = h->format(MarkdownHighlighter::Kind::Link).foreground().color();
        const QColor fm = h->format(MarkdownHighlighter::Kind::Frontmatter).foreground().color();

        QCOMPARE(formatAt(editor, 1, 2).foreground().color(), fm);
        QCOMPARE(formatAt(editor, 3, 2).fontWeight(), int(QFont::Bold));
        QCOMPARE(formatAt(editor, 3, 9).foreground().color(), tag);  // #tag in a heading
        QCOMPARE(formatAt(editor, 4, 7).foreground().color(), link); // inside [[Link]]
        QVERIFY(formatAt(editor, 4, 18).foreground().color() != tag); // #42: all digits
        QCOMPARE(formatAt(editor, 4, 26).foreground().color(), tag);  // #x2
        QVERIFY(formatAt(editor, 4, 32).foreground().color() != tag); // #code inside `code`
        QVERIFY(formatAt(editor, 6, 1).foreground().color() != tag);  // fenced code
        QVERIFY(formatAt(editor, 6, 12).foreground().color() != link);
        QCOMPARE(formatAt(editor, 8, 11).foreground().color(),
                 h->format(MarkdownHighlighter::Kind::BlockId).foreground().color());
    }
};

QTEST_MAIN(TestEditor)
#include "test_editor.moc"
