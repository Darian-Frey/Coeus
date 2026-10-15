#include "ui/MarkdownHighlighter.hpp"

#include "ui/Theme.hpp"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QPalette>
#include <QRegularExpression>
#include <QTextDocument>

namespace coeus {

MarkdownHighlighter::MarkdownHighlighter(QTextDocument* document)
    : QSyntaxHighlighter(document)
{
    restyle();
    connect(&Theme::instance(), &Theme::changed, this, [this] {
        restyle();
        rehighlight();
    });
}

void MarkdownHighlighter::restyle()
{
    const Theme& theme = Theme::instance();
    const QPalette palette = QGuiApplication::palette();
    const QColor dim = palette.color(QPalette::PlaceholderText);
    const QColor accent = theme.colour(Theme::Colour::Accent);
    const QColor tint = theme.isDark() ? QColor(255, 255, 255, 18) : QColor(0, 0, 0, 14);

    auto& heading = m_formats[int(Kind::Heading)];
    heading.setFontWeight(QFont::Bold);
    heading.setForeground(palette.color(QPalette::Text));

    m_formats[int(Kind::Markup)].setForeground(dim);
    m_formats[int(Kind::Emphasis)].setFontItalic(true);
    m_formats[int(Kind::Strong)].setFontWeight(QFont::Bold);
    m_formats[int(Kind::Strike)].setFontStrikeOut(true);
    m_formats[int(Kind::Highlight)].setBackground(theme.colour(Theme::Colour::Highlight));

    auto& code = m_formats[int(Kind::Code)];
    code.setFontFamilies({QFontDatabase::systemFont(QFontDatabase::FixedFont).family()});
    code.setBackground(tint);

    m_formats[int(Kind::Link)].setForeground(accent);
    m_formats[int(Kind::Url)].setForeground(dim);
    m_formats[int(Kind::Tag)].setForeground(accent);
    m_formats[int(Kind::BlockId)].setForeground(dim);
    m_formats[int(Kind::Quote)].setForeground(dim);
    auto& callout = m_formats[int(Kind::Callout)];
    callout.setForeground(accent);
    callout.setFontWeight(QFont::Bold);
    m_formats[int(Kind::ListMarker)].setForeground(accent);
    m_formats[int(Kind::Frontmatter)].setForeground(dim);
    auto& maths = m_formats[int(Kind::Maths)];
    maths.setForeground(accent);
    maths.setBackground(tint);
}

void MarkdownHighlighter::apply(int start, int length, Kind kind)
{
    // Formats combine, so a link inside bold text is both.
    for (int i = start; i < start + length; ++i) {
        QTextCharFormat merged = QSyntaxHighlighter::format(i);
        merged.merge(m_formats[int(kind)]);
        setFormat(i, 1, merged);
    }
}

void MarkdownHighlighter::highlightBlock(const QString& text)
{
    static const QRegularExpression kFence(QStringLiteral(R"(^\s{0,3}(`{3,}|~{3,}))"));
    const int previous = previousBlockState();
    const QString trimmed = text.trimmed();

    // Frontmatter: only from the first line of the document.
    if ((currentBlock().blockNumber() == 0 && text == QLatin1String("---")) || previous == Frontmatter) {
        apply(0, int(text.size()), Kind::Frontmatter);
        const bool closes = previous == Frontmatter && (text == QLatin1String("---") || text == QLatin1String("..."));
        setCurrentBlockState(closes ? Normal : Frontmatter);
        return;
    }

    // Fenced code and display maths run until their closing line.
    if (previous == FenceBacktick || previous == FenceTilde) {
        apply(0, int(text.size()), Kind::Code);
        const QChar fence = previous == FenceBacktick ? QLatin1Char('`') : QLatin1Char('~');
        const bool closes = trimmed.size() >= 3 && trimmed.count(fence) == trimmed.size();
        setCurrentBlockState(closes ? Normal : previous);
        return;
    }
    if (previous == MathsBlock) {
        apply(0, int(text.size()), Kind::Maths);
        setCurrentBlockState(trimmed.endsWith(QLatin1String("$$")) ? Normal : MathsBlock);
        return;
    }
    if (const auto fence = kFence.match(text); fence.hasMatch()) {
        apply(0, int(text.size()), Kind::Code);
        setCurrentBlockState(fence.captured(1).front() == QLatin1Char('`') ? FenceBacktick : FenceTilde);
        return;
    }
    if (trimmed.startsWith(QLatin1String("$$"))) {
        apply(0, int(text.size()), Kind::Maths);
        const bool oneLine = trimmed.size() > 2 && trimmed.endsWith(QLatin1String("$$"));
        setCurrentBlockState(oneLine ? Normal : MathsBlock);
        return;
    }
    setCurrentBlockState(Normal);

    static const QRegularExpression kHeading(QStringLiteral(R"(^\s{0,3}(#{1,6})(\s|$))"));
    static const QRegularExpression kQuote(QStringLiteral(R"(^\s{0,3}(>\s?)+(\[![A-Za-z]+\][-+]?)?)"));
    static const QRegularExpression kList(QStringLiteral(R"(^\s*([-*+]|\d+[.)])\s(\[[ xX]\]\s)?)"));

    int inlineFrom = 0;
    if (const auto heading = kHeading.match(text); heading.hasMatch()) {
        apply(0, int(text.size()), Kind::Heading);
        apply(0, int(heading.capturedLength(1)), Kind::Markup);
        inlineFrom = int(heading.capturedEnd(0));
    } else if (const auto quote = kQuote.match(text); quote.hasMatch()) {
        apply(0, int(text.size()), Kind::Quote);
        if (quote.capturedLength(2) > 0)
            apply(int(quote.capturedStart(2)), int(quote.capturedLength(2)), Kind::Callout);
        inlineFrom = int(quote.capturedEnd(0));
    } else if (const auto list = kList.match(text); list.hasMatch()) {
        apply(int(list.capturedStart(1)), int(list.capturedLength(1)), Kind::ListMarker);
        if (list.capturedLength(2) > 0)
            apply(int(list.capturedStart(2)), int(list.capturedLength(2)), Kind::ListMarker);
        inlineFrom = int(list.capturedEnd(0));
    }
    highlightInline(text, inlineFrom);
}

void MarkdownHighlighter::highlightInline(const QString& text, int from)
{
    struct Rule {
        QRegularExpression pattern;
        Kind kind;
        int markup; // characters at each end drawn as markup
    };
    static const Rule kSpans[] = {
        {QRegularExpression(QStringLiteral(R"((\*\*|__)(?=\S)(.+?)(?<=\S)\1)")), Kind::Strong, 2},
        {QRegularExpression(QStringLiteral(R"((?<![*\w])(\*|_)(?=[^\s*_])(.+?)(?<=[^\s*_])\1(?![*\w]))")), Kind::Emphasis, 1},
        {QRegularExpression(QStringLiteral(R"(~~(?=\S)(.+?)(?<=\S)~~)")), Kind::Strike, 2},
        {QRegularExpression(QStringLiteral(R"(==(?=\S)(.+?)(?<=\S)==)")), Kind::Highlight, 2},
    };
    static const QRegularExpression kWikilink(QStringLiteral(R"(!?\[\[[^\[\]\n]+\]\])"));
    static const QRegularExpression kMarkdownLink(QStringLiteral(R"((!?\[)([^\]\n]*)(\]\()([^)\n]*)(\)))"));
    static const QRegularExpression kTag(QStringLiteral(R"((?<![\w#])#([\p{L}\p{N}_/-]+))"));
    static const QRegularExpression kBlockId(QStringLiteral(R"((?:^|\s)(\^[A-Za-z0-9-]+)\s*$)"));
    static const QRegularExpression kMaths(QStringLiteral(R"((?<![\\$])\$(?=\S)[^$\n]+?(?<=\S)\$(?!\$))"));
    static const QRegularExpression kCode(QStringLiteral(R"((`+)(.+?)\1)"));

    for (const Rule& rule : kSpans) {
        for (auto it = rule.pattern.globalMatch(text, from); it.hasNext();) {
            const auto m = it.next();
            const int start = int(m.capturedStart());
            const int length = int(m.capturedLength());
            apply(start, length, rule.kind);
            apply(start, rule.markup, Kind::Markup);
            apply(start + length - rule.markup, rule.markup, Kind::Markup);
        }
    }
    for (auto it = kWikilink.globalMatch(text, from); it.hasNext();) {
        const auto m = it.next();
        const int start = int(m.capturedStart());
        const int length = int(m.capturedLength());
        const int open = text.at(start) == QLatin1Char('!') ? 3 : 2;
        apply(start, length, Kind::Link);
        apply(start, open, Kind::Markup);
        apply(start + length - 2, 2, Kind::Markup);
    }
    for (auto it = kMarkdownLink.globalMatch(text, from); it.hasNext();) {
        const auto m = it.next();
        apply(int(m.capturedStart(1)), int(m.capturedLength(1)), Kind::Markup);
        apply(int(m.capturedStart(2)), int(m.capturedLength(2)), Kind::Link);
        apply(int(m.capturedStart(3)), int(m.capturedLength(3)), Kind::Markup);
        apply(int(m.capturedStart(4)), int(m.capturedLength(4)), Kind::Url);
        apply(int(m.capturedStart(5)), int(m.capturedLength(5)), Kind::Markup);
    }
    for (auto it = kTag.globalMatch(text, from); it.hasNext();) {
        const auto m = it.next();
        const QString name = m.captured(1);
        if (std::any_of(name.begin(), name.end(), [](QChar c) { return !c.isDigit(); }))
            apply(int(m.capturedStart()), int(m.capturedLength()), Kind::Tag);
    }
    if (const auto m = kBlockId.match(text, from); m.hasMatch())
        apply(int(m.capturedStart(1)), int(m.capturedLength(1)), Kind::BlockId);
    for (auto it = kMaths.globalMatch(text, from); it.hasNext();) {
        const auto m = it.next();
        apply(int(m.capturedStart()), int(m.capturedLength()), Kind::Maths);
    }
    // Code last, replacing anything matched inside it: code is never mined.
    for (auto it = kCode.globalMatch(text, from); it.hasNext();) {
        const auto m = it.next();
        setFormat(int(m.capturedStart()), int(m.capturedLength()), m_formats[int(Kind::Code)]);
    }
}

} // namespace coeus
