// F-003 live preview: the constructs it treats as units, the highlighter's
// hidden syntax, folding and pictures in the editor, Ctrl+click on links, and
// the Default Mode setting. Live preview never changes the text.

#include "parser/LiveBlocks.hpp"
#include "render/MathsRenderer.hpp"
#include "ui/Editor.hpp"
#include "ui/LivePreview.hpp"
#include "ui/MainWindow.hpp"
#include "ui/MarkdownHighlighter.hpp"
#include "ui/NoteView.hpp"
#include "ui/RenderService.hpp"

#include <QAction>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QToolButton>

using namespace coeus;

namespace {

void write(const QString& path, const QByteArray& content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write(content);
}

// The font of character `at` of `block`, as the highlighter left it.
QFont fontAt(const QTextBlock& block, int at)
{
    for (const QTextLayout::FormatRange& range : block.layout()->formats())
        if (at >= range.start && at < range.start + range.length)
            return range.format.font();
    return block.document()->defaultFont();
}

bool hiddenAt(const QTextBlock& block, int at)
{
    return fontAt(block, at).pixelSize() == 1;
}

QAction* actionNamed(MainWindow& window, const QString& text)
{
    for (QAction* action : window.registeredActions())
        if (action->text().remove(QLatin1Char('&')) == text)
            return action;
    return nullptr;
}

} // namespace

class TestLivePreview : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_settings;

private slots:
    void initTestCase()
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    }

    void constructsAreFound()
    {
        const QList<LiveBlock> blocks = LiveBlocks::scan(QStringLiteral(
            "---\ntags: [x]\n---\n"                // 0-2 frontmatter
            "# Title\n"                            // 3
            "```cpp\nint x;\n```\n"                // 4-6 code
            "$$\\sum x$$\n"                        // 7 maths, one line
            "$$\n\\frac{a}{b}\n$$\n"               // 8-10 maths, three lines
            "```mermaid\ngraph LR\n  a-->b\n```\n" // 11-14
            "![[pic.png|Diagram]]\n"               // 15 picture
            "![alt](img/x.jpg)\n"                  // 16 picture
            "![[Some note]]\n"                     // 17 a note embed: not a picture (step 2)
            "Text ![[inline.png]] text\n"          // 18 not alone on its line
            "![[doc.pdf#page=2]]\n"                // 19 a PDF page
            "~~~\nunclosed"));                     // 20-21 a fence to the end
        QCOMPARE(blocks.size(), 9);
        QCOMPARE(blocks[0].kind, LiveBlock::Kind::Frontmatter);
        QCOMPARE(blocks[0].lastLine, 2);
        QCOMPARE(blocks[1].kind, LiveBlock::Kind::Code);
        QCOMPARE(blocks[1].language, QStringLiteral("cpp"));
        QCOMPARE(blocks[1].firstLine, 4);
        QCOMPARE(blocks[1].lastLine, 6);
        QCOMPARE(blocks[2].kind, LiveBlock::Kind::Maths);
        QCOMPARE(blocks[2].source, QStringLiteral("\\sum x"));
        QCOMPARE(blocks[3].firstLine, 8);
        QCOMPARE(blocks[3].lastLine, 10);
        QCOMPARE(blocks[3].source, QStringLiteral("\\frac{a}{b}"));
        QCOMPARE(blocks[4].kind, LiveBlock::Kind::Mermaid);
        QCOMPARE(blocks[4].source, QStringLiteral("graph LR\n  a-->b"));
        QCOMPARE(blocks[5].kind, LiveBlock::Kind::Picture);
        QCOMPARE(blocks[5].source, QStringLiteral("pic.png"));
        QVERIFY(blocks[5].wiki);
        QCOMPARE(blocks[6].source, QStringLiteral("img/x.jpg"));
        QVERIFY(!blocks[6].wiki);
        QCOMPARE(blocks[7].source, QStringLiteral("doc.pdf#page=2"));
        QCOMPARE(blocks[8].kind, LiveBlock::Kind::Code);
        QCOMPARE(blocks[8].lastLine, 21);
    }

    void syntaxHidesAwayFromTheRevealedLines()
    {
        QTextDocument document(QStringLiteral("## A heading\nSome **bold**, [[note|alias]] and `a **b** c`.\nPlain."));
        MarkdownHighlighter highlighter(&document);
        const QTextBlock heading = document.findBlockByNumber(0);
        const QTextBlock line = document.findBlockByNumber(1);
        QVERIFY(!hiddenAt(line, 5)); // source mode hides nothing
        highlighter.setLive(true);
        highlighter.setRevealed(2, 2); // the cursor is on "Plain."
        QVERIFY(hiddenAt(heading, 0) && hiddenAt(heading, 2)); // "## "
        QVERIFY(!hiddenAt(heading, 3));
        QVERIFY(fontAt(heading, 3).pointSizeF() > document.defaultFont().pointSizeF() * 1.3); // sized by level
        QVERIFY(hiddenAt(line, 5) && hiddenAt(line, 6));                                     // "**"
        QVERIFY(!hiddenAt(line, 7));                                                         // "bold"
        const int link = int(line.text().indexOf(QLatin1String("[[")));
        QVERIFY(hiddenAt(line, link) && hiddenAt(line, link + 2)); // "[[" and "note|"
        QVERIFY(!hiddenAt(line, link + 7));                        // "alias"
        const int code = int(line.text().indexOf(QLatin1Char('`')));
        QVERIFY(hiddenAt(line, code));      // a backtick
        QVERIFY(!hiddenAt(line, code + 3)); // "**" inside code stays
        // The cursor's line shows its Markdown, and stops showing it once it leaves.
        highlighter.setRevealed(1, 1);
        QVERIFY(!hiddenAt(line, 5));
        QVERIFY(hiddenAt(heading, 0));
        highlighter.setRevealed(0, 0);
        QVERIFY(hiddenAt(line, 5));
        QVERIFY(!hiddenAt(heading, 0));
        QVERIFY(fontAt(heading, 3).pointSizeF() > document.defaultFont().pointSizeF() * 1.3); // still a heading
        highlighter.setLive(false);
        QVERIFY(!hiddenAt(line, 5));
    }

    void foldingFollowsTheCursorAndNeverTouchesTheText()
    {
        QTemporaryDir vault;
        const QByteArray text = "---\ntags: [x]\n---\n# Title\n\n```cpp\nint x;\n```\n\nEnd **bold**.\n";
        write(vault.filePath(QStringLiteral("n.md")), text);
        Editor editor(*Vault::open(vault.path()), vault.filePath(QStringLiteral("n.md")));
        QVERIFY(editor.load());
        editor.resize(600, 400);
        editor.setLive(true);
        const auto visible = [&editor](int line) { return editor.document()->findBlockByNumber(line).isVisible(); };
        editor.setTextCursor(QTextCursor(editor.document()->findBlockByNumber(9))); // "End **bold**."
        QVERIFY(!visible(0) && !visible(1) && !visible(2)); // frontmatter folded
        QVERIFY(!visible(5) && visible(6) && !visible(7));  // the fence's ``` lines folded, its code shown
        QCOMPARE(editor.live()->revealed(), (QPair<int, int>{9, 9}));
        // Into the code: its fences show; into the frontmatter: it shows.
        editor.setTextCursor(QTextCursor(editor.document()->findBlockByNumber(6)));
        QVERIFY(visible(5) && visible(7));
        editor.setTextCursor(QTextCursor(editor.document()->findBlockByNumber(1)));
        QVERIFY(visible(0) && visible(1) && visible(2));
        // A selection reveals every line it covers.
        QTextCursor selection(editor.document()->findBlockByNumber(3));
        selection.setPosition(editor.document()->findBlockByNumber(9).position(), QTextCursor::KeepAnchor);
        editor.setTextCursor(selection);
        QCOMPARE(editor.live()->revealed(), (QPair<int, int>{3, 9}));
        QVERIFY(visible(5) && visible(7));
        // None of it is an edit.
        QCOMPARE(editor.toPlainText().toUtf8(), text);
        QVERIFY(!editor.isModified());
        QVERIFY(!editor.document()->isUndoAvailable());
        // Typing in live preview is ordinary typing, undone as such.
        editor.setTextCursor(QTextCursor(editor.document()->findBlockByNumber(9)));
        editor.insertPlainText(QStringLiteral("Typed. "));
        QVERIFY(editor.isModified());
        editor.undo();
        QCOMPARE(editor.toPlainText().toUtf8(), text);
        // Back to source: everything shown, nothing hidden.
        editor.setLive(false);
        for (int line = 0; line < editor.document()->blockCount(); ++line)
            QVERIFY(visible(line));
    }

    void picturesTakeTheirHeight()
    {
        if (!MathsRenderer::available())
            QSKIP("No latex or dvisvgm here");
        QTemporaryDir vault;
        write(vault.filePath(QStringLiteral("n.md")), "Before.\n\n$$\n\\frac{a}{b} + \\sqrt{x}\n$$\n\nAfter.\n");
        Editor editor(*Vault::open(vault.path()), vault.filePath(QStringLiteral("n.md")));
        QVERIFY(editor.load());
        editor.resize(600, 400);
        RenderService render;
        render.setVault(vault.path());
        editor.live()->setRenderService(&render);
        editor.setLive(true);
        editor.setTextCursor(QTextCursor(editor.document()->findBlockByNumber(6))); // "After."
        render.waitForDone();
        QTRY_VERIFY(editor.live()->pictureSize(2).isValid()); // drawn, once it landed
        const QSize picture = editor.live()->pictureSize(2);
        const qreal height = editor.blockRect(editor.document()->findBlockByNumber(2)).height();
        QVERIFY2(height >= picture.height() && height < picture.height() + 4 * LivePreview::kPictureMargin + 20,
                 qPrintable(QStringLiteral("line %1 for a picture %2 high").arg(height).arg(picture.height())));
        QVERIFY(!editor.document()->findBlockByNumber(3).isVisible() && !editor.document()->findBlockByNumber(4).isVisible());
        // Into the maths: its source shows, the picture goes.
        editor.setTextCursor(QTextCursor(editor.document()->findBlockByNumber(3)));
        QVERIFY(editor.document()->findBlockByNumber(4).isVisible());
        QVERIFY(!editor.live()->pictureSize(2).isValid());
        QVERIFY(editor.blockRect(editor.document()->findBlockByNumber(2)).height() < picture.height());
    }

    void ctrlClickFollowsLinks()
    {
        QTemporaryDir vault;
        write(vault.filePath(QStringLiteral("a.md")), "See [[b#Part|the part]], [spec](docs/s.md) and https://example.org/x.\n");
        Editor editor(*Vault::open(vault.path()), vault.filePath(QStringLiteral("a.md")));
        QVERIFY(editor.load());
        const QString line = editor.document()->firstBlock().text();
        const auto at = [&line](const char* needle) { return int(line.indexOf(QLatin1String(needle))) + 1; };
        QCOMPARE(editor.linkAt(at("the part")), QUrl(QStringLiteral("coeus-wiki:b#Part")));
        QCOMPARE(editor.linkAt(at("spec")), QUrl(QStringLiteral("coeus-file:docs/s.md")));
        QCOMPARE(editor.linkAt(at("example")), QUrl(QStringLiteral("https://example.org/x.")));
        QVERIFY(editor.linkAt(at("See")).isEmpty());
        QCOMPARE(editor.linkAt(at("[[b#") - 1), QUrl(QStringLiteral("coeus-wiki:b#Part"))); // on the brackets too

        // In the window: Ctrl+click opens; a plain click only moves the cursor.
        write(vault.filePath(QStringLiteral("b.md")), "# B\n\n## Part\n\nHere.\n");
        MainWindow window;
        window.resize(900, 600);
        window.show();
        QSignalSpy opened(&window, &MainWindow::indexOpened);
        window.openVault(*Vault::open(vault.path()));
        QVERIFY(opened.wait(5000));
        window.openNote(vault.filePath(QStringLiteral("a.md")));
        Editor* inWindow = window.currentEditor();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTextCursor cursor(inWindow->document());
        cursor.setPosition(at("the part") + 2);
        const QPoint point = inWindow->cursorRect(cursor).center();
        QTest::mouseClick(inWindow->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QCOMPARE(window.currentEditor()->path(), vault.filePath(QStringLiteral("a.md")));
        QTest::mouseClick(inWindow->viewport(), Qt::LeftButton, Qt::ControlModifier, point);
        QCOMPARE(window.currentEditor()->path(), vault.filePath(QStringLiteral("b.md")));
        QCOMPARE(window.currentEditor()->textCursor().blockNumber() + 1, 3); // at "## Part"
    }

    void notesOpenInTheDefaultMode()
    {
        QTemporaryDir vault;
        write(vault.filePath(QStringLiteral("a.md")), "# A\n");
        write(vault.filePath(QStringLiteral("b.md")), "# B\n");
        MainWindow window;
        window.openVault(*Vault::open(vault.path()));
        QCOMPARE(window.defaultMode(), NoteView::Mode::Live); // at first
        window.openNote(vault.filePath(QStringLiteral("a.md")));
        NoteView* view = window.currentView();
        QCOMPARE(view->mode(), NoteView::Mode::Live);
        QVERIFY(view->editor()->isLive());
        QVERIFY(view->liveButton()->isChecked());
        // Ctrl+E goes to reading and back to the mode it came from.
        view->toggleMode();
        QCOMPARE(view->mode(), NoteView::Mode::Reading);
        view->toggleMode();
        QCOMPARE(view->mode(), NoteView::Mode::Live);
        view->setMode(NoteView::Mode::Source);
        QVERIFY(!view->editor()->isLive());
        view->toggleMode();
        view->toggleMode();
        QCOMPARE(view->mode(), NoteView::Mode::Source);

        QAction* source = actionNamed(window, QStringLiteral("Default Mode: Source"));
        QVERIFY(source);
        source->trigger();
        QCOMPARE(QSettings().value(QStringLiteral("editor/defaultMode")).toString(), QStringLiteral("source"));
        window.openNote(vault.filePath(QStringLiteral("b.md")));
        QCOMPARE(window.currentView()->mode(), NoteView::Mode::Source);
        actionNamed(window, QStringLiteral("Default Mode: Live"))->trigger();
    }
};

QTEST_MAIN(TestLivePreview)
#include "test_live_preview.moc"
