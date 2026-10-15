#include "ui/MarkdownHighlighter.hpp"

#include "ui/Theme.hpp"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QPalette>
#include <QRegularExpression>
#include <QTextBlock>
#include <QSet>
#include <QFontMetrics>
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

void MarkdownHighlighter::setLive(bool live)
{
    if (live == m_live)
        return;
    m_live = live;
    rehighlight();
}

void MarkdownHighlighter::setRevealed(int first, int last)
{
    if (first == m_revealFirst && last == m_revealLast)
        return;
    const int oldFirst = m_revealFirst;
    const int oldLast = m_revealLast;
    m_revealFirst = first;
    m_revealLast = last;
    if (!m_live)
        return;
    // Only the lines that change between shown and hidden.
    for (const auto& [from, to] : {std::pair{oldFirst, oldLast}, std::pair{first, last}}) {
        for (int n = std::max(0, from); n <= to; ++n) {
            const QTextBlock block = document()->findBlockByNumber(n);
            if (block.isValid())
                rehighlightBlock(block);
        }
    }
}

void MarkdownHighlighter::setTallLines(const QHash<int, int>& heights)
{
    if (heights == m_tall)
        return;
    QSet<int> changed;
    for (auto it = m_tall.cbegin(); it != m_tall.cend(); ++it)
        if (heights.value(it.key(), -1) != it.value())
            changed.insert(it.key());
    for (auto it = heights.cbegin(); it != heights.cend(); ++it)
        if (m_tall.value(it.key(), -1) != it.value())
            changed.insert(it.key());
    m_tall = heights;
    if (!m_live)
        return;
    for (int n : std::as_const(changed)) {
        const QTextBlock block = document()->findBlockByNumber(n);
        if (block.isValid())
            rehighlightBlock(block);
    }
}

void MarkdownHighlighter::hideLive(int start, int length)
{
    if (length > 0)
        m_hidden.append({start, length});
}

void MarkdownHighlighter::apply(int start, int length, Kind kind)
{
    if (kind == Kind::Markup)
        hideLive(start, length);
    // Formats combine, so a link inside bold text is both.
    for (int i = start; i < start + length; ++i) {
        QTextCharFormat merged = QSyntaxHighlighter::format(i);
        merged.merge(m_formats[int(kind)]);
        setFormat(i, 1, merged);
    }
}

void MarkdownHighlighter::highlightBlock(const QString& text)
{
    m_hidden.clear();
    m_headingLevel = 0;
    highlightSource(text);
    if (!m_live)
        return;
    const int number = currentBlock().blockNumber();
    const QFont base = document()->defaultFont();
    if (const auto tall = m_tall.constFind(number); tall != m_tall.cend() && !isRevealed(number)) {
        // The line stands for a picture: as tall as it, its text out of sight.
        QFont font = base;
        int pixels = std::max(8, *tall);
        font.setPixelSize(pixels);
        // Line spacing is a little more than the pixel size; shrink to fit.
        const int spacing = QFontMetrics(font).lineSpacing();
        font.setPixelSize(std::max(4, int(double(pixels) * pixels / std::max(1, spacing))));
        font.setStretch(1);
        QTextCharFormat tallFormat;
        tallFormat.setFont(font, QTextCharFormat::FontPropertiesSpecifiedOnly);
        tallFormat.setForeground(Qt::transparent);
        setFormat(0, int(text.size()), tallFormat);
        return;
    }
    if (m_headingLevel > 0) {
        // Headings read as headings, shown or not.
        static constexpr double kScale[] = {1.0, 1.8, 1.5, 1.3, 1.15, 1.05, 1.0};
        const double size = (base.pointSizeF() > 0 ? base.pointSizeF() : 10.0) * kScale[m_headingLevel];
        for (int i = 0; i < text.size(); ++i) {
            QTextCharFormat sized = QSyntaxHighlighter::format(i);
            sized.setFontPointSize(size);
            setFormat(i, 1, sized);
        }
    }
    if (isRevealed(number))
        return;
    QFont tiny = base;
    tiny.setPixelSize(1);
    tiny.setStretch(1);
    QTextCharFormat hidden;
    hidden.setFont(tiny, QTextCharFormat::FontPropertiesSpecifiedOnly);
    hidden.setForeground(Qt::transparent);
    for (const auto& [start, length] : std::as_const(m_hidden))
        for (int i = start; i < start + length && i < text.size(); ++i) {
            QTextCharFormat merged = QSyntaxHighlighter::format(i);
            merged.merge(hidden);
            setFormat(i, 1, merged);
        }
}

void MarkdownHighlighter::highlightSource(const QString& text)
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
        hideLive(0, int(heading.capturedEnd(0))); // the marker and its space
        m_headingLevel = int(heading.capturedLength(1));
        inlineFrom = int(heading.capturedEnd(0));
    } else if (const auto quote = kQuote.match(text); quote.hasMatch()) {
        apply(0, int(text.size()), Kind::Quote);
        if (quote.capturedLength(2) == 0)
            hideLive(0, int(quote.capturedEnd(0))); // a plain quote's ">"; a callout keeps its [!kind]
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
        // [[target|alias]] reads as its alias.
        if (const qsizetype pipe = text.indexOf(QLatin1Char('|'), start); pipe > start && pipe < start + length - 2)
            hideLive(start + open, int(pipe - start - open + 1));
    }
    for (auto it = kMarkdownLink.globalMatch(text, from); it.hasNext();) {
        const auto m = it.next();
        apply(int(m.capturedStart(1)), int(m.capturedLength(1)), Kind::Markup);
        apply(int(m.capturedStart(2)), int(m.capturedLength(2)), Kind::Link);
        apply(int(m.capturedStart(3)), int(m.capturedLength(3)), Kind::Markup);
        apply(int(m.capturedStart(4)), int(m.capturedLength(4)), Kind::Url);
        apply(int(m.capturedStart(5)), int(m.capturedLength(5)), Kind::Markup);
        hideLive(int(m.capturedStart(4)), int(m.capturedLength(4))); // [text](url) reads as its text
    }
    for (auto it = kTag.globalMatch(text, from); it.hasNext();) {
        const auto m = it.next();
        const QString name = m.captured(1);
        if (std::any_of(name.begin(), name.end(), [](QChar c) { return !c.isDigit(); }))
            apply(int(m.capturedStart()), int(m.capturedLength()), Kind::Tag);
    }
    if (const auto m = kBlockId.match(text, from); m.hasMatch()) {
        apply(int(m.capturedStart(1)), int(m.capturedLength(1)), Kind::BlockId);
        hideLive(int(m.capturedStart(1)), int(m.capturedLength(1)));
    }
    for (auto it = kMaths.globalMatch(text, from); it.hasNext();) {
        const auto m = it.next();
        apply(int(m.capturedStart()), int(m.capturedLength()), Kind::Maths);
        hideLive(int(m.capturedStart()), 1);
        hideLive(int(m.capturedEnd()) - 1, 1);
    }
    // Code last, replacing anything matched inside it: code is never mined.
    for (auto it = kCode.globalMatch(text, from); it.hasNext();) {
        const auto m = it.next();
        setFormat(int(m.capturedStart()), int(m.capturedLength()), m_formats[int(Kind::Code)]);
        // Nothing inside code is hidden but its backticks.
        const int start = int(m.capturedStart());
        const int end = int(m.capturedEnd());
        m_hidden.removeIf([start, end](const QPair<int, int>& r) { return r.first >= start && r.first < end; });
        hideLive(start, int(m.capturedLength(1)));
        hideLive(end - int(m.capturedLength(1)), int(m.capturedLength(1)));
    }
}

} // namespace coeus
