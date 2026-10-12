#include "parser/NoteParser.hpp"

#include "parser/Frontmatter.hpp"
#include "parser/LineIndex.hpp"

#include <QRegularExpression>
#include <QUrl>

#include <md4c.h>

#include <vector>

namespace coeus {

namespace {

constexpr unsigned kParserFlags = MD_DIALECT_GITHUB | MD_FLAG_WIKILINKS | MD_FLAG_LATEXMATHSPANS;

bool isWordChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

// A heading or text block whose plain text is collected for block IDs and
// heading titles.
struct TextBlock {
    MD_BLOCKTYPE type;
    QString text;
    int firstLine = 0;
    int lastLine = 0;
};

// An open link span whose visible text becomes the link's alias.
struct OpenLink {
    Link link;
    bool hasLabel = false;
    QString label;
};

class Walker {
public:
    Walker(QByteArrayView body, int firstLine, ParsedNote& note)
        : m_body(body)
        , m_lines(body)
        , m_firstLine(firstLine)
        , m_note(note)
        , m_line(firstLine)
    {
    }

    void run()
    {
        MD_PARSER parser{};
        parser.flags = kParserFlags;
        parser.enter_block = [](MD_BLOCKTYPE t, void* d, void* self) { return static_cast<Walker*>(self)->enterBlock(t, d); };
        parser.leave_block = [](MD_BLOCKTYPE t, void* d, void* self) { return static_cast<Walker*>(self)->leaveBlock(t, d); };
        parser.enter_span = [](MD_SPANTYPE t, void* d, void* self) { return static_cast<Walker*>(self)->enterSpan(t, d); };
        parser.leave_span = [](MD_SPANTYPE t, void* d, void* self) { return static_cast<Walker*>(self)->leaveSpan(t, d); };
        parser.text = [](MD_TEXTTYPE t, const MD_CHAR* s, MD_SIZE n, void* self) {
            return static_cast<Walker*>(self)->text(t, s, n);
        };
        md_parse(m_body.data(), MD_SIZE(m_body.size()), &parser, this);
    }

private:
    // Line of a pointer into the body; text md4c synthesises (entities,
    // line breaks) lies outside it and keeps the last known line.
    int lineOf(const MD_CHAR* p)
    {
        if (p >= m_body.data() && p < m_body.data() + m_body.size())
            m_line = m_firstLine + m_lines.lineAt(p - m_body.data()) - 1;
        return m_line;
    }

    QString attribute(const MD_ATTRIBUTE& a) const
    {
        return QString::fromUtf8(a.text, qsizetype(a.size));
    }

    char sourceAt(const MD_CHAR* p, qsizetype delta) const
    {
        const qsizetype offset = (p - m_body.data()) + delta;
        return offset >= 0 && offset < m_body.size() ? m_body[offset] : '\0';
    }

    int enterBlock(MD_BLOCKTYPE type, void* detail)
    {
        m_previous = QChar(); // a new block starts a new word
        switch (type) {
        case MD_BLOCK_H:
        case MD_BLOCK_P:
        case MD_BLOCK_LI:
        case MD_BLOCK_TH:
        case MD_BLOCK_TD:
            m_blocks.push_back(TextBlock{type, QString(), 0, m_line});
            if (type == MD_BLOCK_H)
                m_headingLevel = int(static_cast<MD_BLOCK_H_DETAIL*>(detail)->level);
            break;
        case MD_BLOCK_CODE:
            ++m_codeDepth;
            break;
        default:
            break;
        }
        return 0;
    }

    int leaveBlock(MD_BLOCKTYPE type, void*)
    {
        m_previous = QChar();
        if (type == MD_BLOCK_CODE) {
            --m_codeDepth;
            m_note.plainText.append(QLatin1Char('\n'));
            return 0;
        }
        if (m_blocks.empty() || m_blocks.back().type != type)
            return 0;

        TextBlock block = std::move(m_blocks.back());
        m_blocks.pop_back();
        m_note.plainText.append(QLatin1Char('\n'));

        if (type == MD_BLOCK_H) {
            const QString text = block.text.simplified();
            if (!text.isEmpty())
                m_note.headings.append(Heading{m_headingLevel, text, block.firstLine});
        } else if (type == MD_BLOCK_P || type == MD_BLOCK_LI) {
            // ^block-id at the end of a paragraph or list item.
            static const QRegularExpression kBlockId(QStringLiteral(R"((?:^|\s)\^([A-Za-z0-9-]+)\s*$)"));
            if (const auto match = kBlockId.match(block.text); match.hasMatch())
                m_note.blocks.append(BlockId{match.captured(1), block.lastLine});
        }
        return 0;
    }

    int enterSpan(MD_SPANTYPE type, void* detail)
    {
        switch (type) {
        case MD_SPAN_WIKILINK: {
            const MD_ATTRIBUTE& target = static_cast<MD_SPAN_WIKILINK_DETAIL*>(detail)->target;
            OpenLink open;
            open.link.style = LinkStyle::Wiki;
            open.link.line = lineOf(target.text);
            open.link.embed = sourceAt(target.text, -3) == '!';
            open.hasLabel = sourceAt(target.text, qsizetype(target.size)) == '|';
            splitTarget(attribute(target), open.link);
            m_links.push_back(open);
            break;
        }
        case MD_SPAN_A:
        case MD_SPAN_IMG: {
            const MD_ATTRIBUTE& href = type == MD_SPAN_A ? static_cast<MD_SPAN_A_DETAIL*>(detail)->href
                                                         : static_cast<MD_SPAN_IMG_DETAIL*>(detail)->src;
            OpenLink open;
            open.link.style = LinkStyle::Markdown;
            open.link.line = lineOf(href.text);
            open.link.embed = type == MD_SPAN_IMG;
            open.hasLabel = true;
            const QString raw = attribute(href);
            if (!isLocal(raw)) {
                m_links.push_back(OpenLink{}); // keep spans balanced; target stays empty
                break;
            }
            const qsizetype hash = raw.indexOf(QLatin1Char('#'));
            open.link.target = QUrl::fromPercentEncoding(raw.left(hash < 0 ? raw.size() : hash).toUtf8());
            if (hash >= 0)
                open.link.heading = QUrl::fromPercentEncoding(raw.mid(hash + 1).toUtf8());
            m_links.push_back(open);
            break;
        }
        case MD_SPAN_CODE:
        case MD_SPAN_LATEXMATH:
        case MD_SPAN_LATEXMATH_DISPLAY:
            ++m_codeDepth;
            break;
        default:
            break;
        }
        return 0;
    }

    int leaveSpan(MD_SPANTYPE type, void*)
    {
        switch (type) {
        case MD_SPAN_WIKILINK:
        case MD_SPAN_A:
        case MD_SPAN_IMG: {
            if (m_links.empty())
                break;
            OpenLink open = std::move(m_links.back());
            m_links.pop_back();
            if (open.link.target.isEmpty())
                break;
            if (open.hasLabel)
                open.link.alias = open.label.simplified();
            m_note.links.append(open.link);
            break;
        }
        case MD_SPAN_CODE:
        case MD_SPAN_LATEXMATH:
        case MD_SPAN_LATEXMATH_DISPLAY:
            --m_codeDepth;
            break;
        default:
            break;
        }
        return 0;
    }

    int text(MD_TEXTTYPE type, const MD_CHAR* data, MD_SIZE size)
    {
        const int line = lineOf(data);
        QString chunk;
        switch (type) {
        case MD_TEXT_SOFTBR:
        case MD_TEXT_BR:
            chunk = QStringLiteral("\n");
            break;
        case MD_TEXT_NULLCHAR:
            chunk = QChar(0xFFFD);
            break;
        default:
            chunk = QString::fromUtf8(data, qsizetype(size));
            break;
        }

        m_note.plainText.append(chunk);
        for (OpenLink& open : m_links)
            open.label.append(chunk);
        if (!m_blocks.empty()) {
            TextBlock& block = m_blocks.back();
            // Code inside a paragraph is not a block ID carrier; headings keep it.
            block.text.append(m_codeDepth > 0 && block.type != MD_BLOCK_H ? QStringLiteral(" ") : chunk);
            if (block.firstLine == 0)
                block.firstLine = line;
            block.lastLine = line;
        }

        if (type == MD_TEXT_NORMAL && m_codeDepth == 0 && m_links.empty())
            scanTags(scanEmbeds(chunk, line), line);
        m_previous = chunk.isEmpty() ? m_previous : chunk.back();
        return 0;
    }

    // md4c reads "![" as an image opener, so ![[target]] embeds reach us as
    // plain text (D-014). Records them and returns the chunk with each embed
    // blanked out, so "#Heading" inside one is not taken for a tag.
    QString scanEmbeds(const QString& chunk, int line)
    {
        static const QRegularExpression kEmbed(QStringLiteral(R"(!\[\[([^\[\]|\n]+)(?:\|([^\[\]\n]*))?\]\])"));
        QString masked = chunk;
        for (auto it = kEmbed.globalMatch(chunk); it.hasNext();) {
            const QRegularExpressionMatch match = it.next();
            Link link;
            link.style = LinkStyle::Wiki;
            link.embed = true;
            link.line = line + int(chunk.left(match.capturedStart()).count(QLatin1Char('\n')));
            link.alias = match.captured(2).simplified();
            splitTarget(match.captured(1), link);
            if (!link.target.isEmpty())
                m_note.links.append(link);
            masked.replace(match.capturedStart(), match.capturedLength(), QString(match.capturedLength(), QLatin1Char(' ')));
        }
        return masked;
    }

    // #tag not preceded by a word character, with at least one non-digit
    // (SPEC.md §Markdown dialect). Lines inside a run are tracked by '\n'.
    void scanTags(const QString& chunk, int line)
    {
        QChar before = m_previous;
        for (qsizetype i = 0; i < chunk.size(); ++i) {
            const QChar c = chunk[i];
            if (c == QLatin1Char('#') && !isWordChar(before) && before != QLatin1Char('#')) {
                qsizetype end = i + 1;
                bool hasNonDigit = false;
                while (end < chunk.size() && (isWordChar(chunk[end]) || chunk[end] == QLatin1Char('/')
                                              || chunk[end] == QLatin1Char('-'))) {
                    if (!chunk[end].isDigit())
                        hasNonDigit = true;
                    ++end;
                }
                QString name = chunk.mid(i + 1, end - i - 1);
                while (name.endsWith(QLatin1Char('/')))
                    name.chop(1);
                if (hasNonDigit && !name.isEmpty() && (end == chunk.size() || chunk[end] != QLatin1Char('#'))) {
                    const int tagLine = line + int(chunk.left(i).count(QLatin1Char('\n')));
                    m_note.tags.append(Tag{name, tagLine});
                }
                i = end - 1;
                before = chunk[i];
                continue;
            }
            before = c;
        }
    }

    static bool isLocal(const QString& href)
    {
        if (href.isEmpty() || href.startsWith(QLatin1Char('#')))
            return false;
        static const QRegularExpression kScheme(QStringLiteral(R"(^[A-Za-z][A-Za-z0-9+.-]*:)"));
        return !kScheme.match(href).hasMatch() && !href.startsWith(QLatin1String("//"));
    }

    // "Note#Heading", "Note#^block" or "Note^block" (SPEC.md §Markdown dialect).
    static void splitTarget(const QString& raw, Link& link)
    {
        QString target = raw;
        if (const qsizetype hash = target.indexOf(QLatin1Char('#')); hash >= 0) {
            const QString fragment = target.mid(hash + 1);
            target.truncate(hash);
            if (fragment.startsWith(QLatin1Char('^')))
                link.block = fragment.mid(1);
            else
                link.heading = fragment;
        } else if (const qsizetype caret = target.indexOf(QLatin1Char('^')); caret >= 0) {
            link.block = target.mid(caret + 1);
            target.truncate(caret);
        }
        link.target = target.trimmed();
        link.heading = link.heading.trimmed();
        link.block = link.block.trimmed();
    }

    QByteArrayView m_body;
    LineIndex m_lines;
    int m_firstLine;
    ParsedNote& m_note;

    int m_line;
    int m_codeDepth = 0;
    int m_headingLevel = 1;
    QChar m_previous;
    std::vector<TextBlock> m_blocks;
    std::vector<OpenLink> m_links;
};

} // namespace

ParsedNote NoteParser::parse(QByteArrayView utf8)
{
    ParsedNote note;
    const Frontmatter::Split split = Frontmatter::split(utf8);
    note.bodyStartLine = split.bodyFirstLine;
    Frontmatter::parse(split, note);

    Walker(split.body, split.bodyFirstLine, note).run();
    note.plainText = note.plainText.trimmed();
    return note;
}

} // namespace coeus
