#pragma once

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

protected:
    void highlightBlock(const QString& text) override;

private:
    enum State { Normal = -1, Frontmatter = 1, FenceBacktick = 2, FenceTilde = 3, MathsBlock = 4 };

    void highlightInline(const QString& text, int from);
    void apply(int start, int length, Kind kind);

    QTextCharFormat m_formats[int(Kind::Maths) + 1];
};

} // namespace coeus
