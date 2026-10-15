#pragma once

#include <QHash>
#include <QList>
#include <QPair>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

namespace coeus {

// Source-mode highlighting (F-003) for CommonMark plus the extensions in
// SPEC.md §Markdown dialect. Works line by line; fenced code, frontmatter and
// display maths carry across lines through the block state. Its recognition
// rules mirror parser/ (tags need a non-digit; code is never mined), but it is
// a display aid only: the index never depends on it.
class MarkdownHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    enum class Kind {
        Heading, Markup, Emphasis, Strong, Strike, Highlight, Code, Link, Url,
        Tag, BlockId, Quote, Callout, ListMarker, Frontmatter, Maths,
    };

    explicit MarkdownHighlighter(QTextDocument* document);

    const QTextCharFormat& format(Kind kind) const { return m_formats[int(kind)]; }

    // Live preview (F-003, SPEC.md §Live preview): headings sized, and on
    // lines other than the revealed ones the syntax hidden, so the text reads
    // as rendered while it stays the text.
    void setLive(bool live);
    bool isLive() const { return m_live; }
    // The lines shown as written: the cursor's or the selection's, 0-based.
    void setRevealed(int first, int last);
    // Lines that stand for a picture drawn over them, with the height to
    // reserve in pixels; their text is hidden.
    void setTallLines(const QHash<int, int>& heights);
    bool isRevealed(int block) const { return block >= m_revealFirst && block <= m_revealLast; }

protected:
    void highlightBlock(const QString& text) override;

private:
    enum State { Normal = -1, Frontmatter = 1, FenceBacktick = 2, FenceTilde = 3, MathsBlock = 4 };

    // The formats' colours, from the theme (F-027); again when it changes.
    void restyle();
    void highlightInline(const QString& text, int from);
    void highlightSource(const QString& text);
    void apply(int start, int length, Kind kind);
    void hideLive(int start, int length); // hidden in live preview away from the cursor

    QTextCharFormat m_formats[int(Kind::Maths) + 1];
    bool m_live = false;
    int m_revealFirst = -1;
    int m_revealLast = -1;
    QHash<int, int> m_tall;
    QList<QPair<int, int>> m_hidden; // this block's ranges to hide in live preview
    int m_headingLevel = 0;          // this block's, 0 if it is not a heading
};

} // namespace coeus
