#include "parser/HtmlRenderer.hpp"

#include "parser/Frontmatter.hpp"
#include "parser/ParsedNote.hpp"
#include "parser/Attachments.hpp"
#include "parser/CodeHighlighter.hpp"
#include "parser/Transclusion.hpp"

#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QUrl>

#include <md4c.h>

#include <vector>

namespace coeus {

namespace {

constexpr unsigned kParserFlags = MD_DIALECT_GITHUB | MD_FLAG_WIKILINKS | MD_FLAG_LATEXMATHSPANS;

// Callout kinds and their accent colours (SPEC.md §Markdown dialect).
QString calloutColour(const QString& kind)
{
    static const QHash<QString, QString> kColours{
        {QStringLiteral("note"), QStringLiteral("#2f7fd0")},     {QStringLiteral("info"), QStringLiteral("#2f7fd0")},
        {QStringLiteral("tip"), QStringLiteral("#1f9a8a")},      {QStringLiteral("abstract"), QStringLiteral("#1f9a8a")},
        {QStringLiteral("example"), QStringLiteral("#7a5fc0")},  {QStringLiteral("question"), QStringLiteral("#c08a1f")},
        {QStringLiteral("warning"), QStringLiteral("#c07a1f")},  {QStringLiteral("danger"), QStringLiteral("#c0392b")},
        {QStringLiteral("quote"), QStringLiteral("#7f8c8d")},
    };
    return kColours.value(kind.toLower(), QStringLiteral("#2f7fd0"));
}

QString encodeTarget(const QString& target)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(target, "/:"));
}

class Writer {
public:
    Writer(QByteArrayView body, const HtmlRenderer::Options& options, const QString& sourcePath, int depth,
           const QStringList& ancestors)
        : m_body(body)
        , m_options(options)
        , m_sourcePath(sourcePath)
        , m_depth(depth)
        , m_ancestors(ancestors)
    {
        m_out.emplace_back();
    }

    QString run()
    {
        MD_PARSER parser{};
        parser.flags = kParserFlags;
        parser.enter_block = [](MD_BLOCKTYPE t, void* d, void* self) { return static_cast<Writer*>(self)->enterBlock(t, d); };
        parser.leave_block = [](MD_BLOCKTYPE t, void* d, void* self) { return static_cast<Writer*>(self)->leaveBlock(t, d); };
        parser.enter_span = [](MD_SPANTYPE t, void* d, void* self) { return static_cast<Writer*>(self)->enterSpan(t, d); };
        parser.leave_span = [](MD_SPANTYPE t, void* d, void* self) { return static_cast<Writer*>(self)->leaveSpan(t, d); };
        parser.text = [](MD_TEXTTYPE t, const MD_CHAR* s, MD_SIZE n, void* self) {
            return static_cast<Writer*>(self)->text(t, s, n);
        };
        md_parse(m_body.data(), MD_SIZE(m_body.size()), &parser, this);

        // Block IDs mark a paragraph or list item for linking; they are not shown.
        static const QRegularExpression kBlockId(QStringLiteral(R"(\s\^[A-Za-z0-9-]+\s*(</p>|</li>))"));
        QString html = m_out.front();
        html.replace(kBlockId, QStringLiteral("\\1"));
        return html;
    }

private:
    QString& out() { return m_out.back(); }

    static QString attribute(const MD_ATTRIBUTE& a)
    {
        return QString::fromUtf8(a.text, qsizetype(a.size));
    }

    // --- Blocks --------------------------------------------------------------

    int enterBlock(MD_BLOCKTYPE type, void* detail)
    {
        switch (type) {
        case MD_BLOCK_DOC:
            break;
        case MD_BLOCK_QUOTE:
            m_out.emplace_back(); // rendered later: it may be a callout
            break;
        case MD_BLOCK_UL:
            m_out.emplace_back(); // rendered later: a task list drops its bullets
            break;
        case MD_BLOCK_OL: {
            const auto* ol = static_cast<MD_BLOCK_OL_DETAIL*>(detail);
            out() += ol->start == 1 ? QStringLiteral("<ol>") : QStringLiteral("<ol start=\"%1\">").arg(ol->start);
            break;
        }
        case MD_BLOCK_LI: {
            const auto* li = static_cast<MD_BLOCK_LI_DETAIL*>(detail);
            out() += QStringLiteral("<li>");
            if (li->is_task)
                out() += li->task_mark == ' ' ? QStringLiteral("☐ ") : QStringLiteral("☑ ");
            break;
        }
        case MD_BLOCK_HR:
            out() += QStringLiteral("<hr/>");
            break;
        case MD_BLOCK_H:
            out() += QStringLiteral("<h%1>").arg(static_cast<MD_BLOCK_H_DETAIL*>(detail)->level);
            break;
        case MD_BLOCK_CODE: {
            const auto* code = static_cast<MD_BLOCK_CODE_DETAIL*>(detail);
            const QString lang = attribute(code->lang);
            out() += lang.isEmpty() ? QStringLiteral("<pre><code>")
                                    : QStringLiteral("<pre class=\"lang-%1\"><code>").arg(HtmlRenderer::escape(lang));
            ++m_codeDepth;
            // Collected whole, then coloured: a string or comment may span lines (F-025).
            m_codeBlock = true;
            m_codeLanguage = lang;
            m_codeText.clear();
            break;
        }
        case MD_BLOCK_HTML:
            out() += QStringLiteral("<pre class=\"html\"><code>");
            ++m_codeDepth;
            break;
        case MD_BLOCK_P:
            out() += QStringLiteral("<p>");
            ++m_paragraphDepth;
            break;
        case MD_BLOCK_TABLE:
            out() += QStringLiteral("<table class=\"grid\" cellspacing=\"0\" cellpadding=\"4\">");
            break;
        case MD_BLOCK_THEAD:
        case MD_BLOCK_TBODY:
            break;
        case MD_BLOCK_TR:
            out() += QStringLiteral("<tr>");
            break;
        case MD_BLOCK_TH:
        case MD_BLOCK_TD: {
            const auto* cell = static_cast<MD_BLOCK_TD_DETAIL*>(detail);
            const char* align = cell->align == MD_ALIGN_CENTER ? " align=\"center\""
                                : cell->align == MD_ALIGN_RIGHT ? " align=\"right\""
                                                                : "";
            out() += QStringLiteral("<%1%2>").arg(QLatin1String(type == MD_BLOCK_TH ? "th" : "td"), QLatin1String(align));
            break;
        }
        }
        return 0;
    }

    int leaveBlock(MD_BLOCKTYPE type, void* detail)
    {
        switch (type) {
        case MD_BLOCK_DOC:
            break;
        case MD_BLOCK_QUOTE: {
            QString inner = std::move(m_out.back());
            m_out.pop_back();
            out() += quoteOrCallout(inner);
            break;
        }
        case MD_BLOCK_UL: {
            const QString items = std::move(m_out.back());
            m_out.pop_back();
            // Every item a task: the checkbox is the marker, as in Obsidian.
            const bool tasks = items.count(QStringLiteral("<li>")) > 0
                               && items.count(QStringLiteral("<li>")) == items.count(QStringLiteral("<li>☐ "))
                                                                            + items.count(QStringLiteral("<li>☑ "));
            out() += (tasks ? QStringLiteral("<ul style=\"list-style-type: none;\">") : QStringLiteral("<ul>")) + items
                     + QStringLiteral("</ul>");
            break;
        }
        case MD_BLOCK_OL:
            out() += QStringLiteral("</ol>");
            break;
        case MD_BLOCK_LI:
            out() += QStringLiteral("</li>");
            break;
        case MD_BLOCK_HR:
            break;
        case MD_BLOCK_H:
            out() += QStringLiteral("</h%1>").arg(static_cast<MD_BLOCK_H_DETAIL*>(detail)->level);
            break;
        case MD_BLOCK_CODE:
            if (m_codeText.endsWith(QLatin1Char('\n')))
                m_codeText.chop(1);
            out() += CodeHighlighter::toHtml(m_codeText, m_codeLanguage) + QStringLiteral("</code></pre>");
            m_codeBlock = false;
            --m_codeDepth;
            break;
        case MD_BLOCK_HTML:
            out() += QStringLiteral("</code></pre>");
            --m_codeDepth;
            break;
        case MD_BLOCK_P:
            out() += QStringLiteral("</p>");
            --m_paragraphDepth;
            break;
        case MD_BLOCK_TABLE:
            out() += QStringLiteral("</table>");
            break;
        case MD_BLOCK_THEAD:
        case MD_BLOCK_TBODY:
            break;
        case MD_BLOCK_TR:
            out() += QStringLiteral("</tr>");
            break;
        case MD_BLOCK_TH:
            out() += QStringLiteral("</th>");
            break;
        case MD_BLOCK_TD:
            out() += QStringLiteral("</td>");
            break;
        }
        return 0;
    }

    // "> [!kind]- Title" becomes a coloured box; any other quote stays a quote.
    static QString quoteOrCallout(const QString& inner)
    {
        static const QRegularExpression kHead(QStringLiteral(R"(^<p>\[!([A-Za-z]+)\]([-+])?[ \t]*([^<\n]*)(<br/>|</p>))"));
        const auto head = kHead.match(inner);
        if (!head.hasMatch())
            return QStringLiteral("<blockquote>") + inner + QStringLiteral("</blockquote>");

        const QString kind = head.captured(1);
        QString title = head.captured(3).trimmed();
        if (title.isEmpty())
            title = kind.left(1).toUpper() + kind.mid(1).toLower();
        QString body = inner.mid(head.capturedEnd());
        if (head.captured(4) == QLatin1String("<br/>"))
            body.prepend(QStringLiteral("<p>"));
        const QString colour = calloutColour(kind);
        return QStringLiteral("<table class=\"callout\" width=\"100%\" cellspacing=\"0\" cellpadding=\"8\">"
                              "<tr><td style=\"border-left: 3px solid %1;\">"
                              "<p class=\"callout-title\" style=\"color: %1;\"><b>%2</b></p>%3</td></tr></table>")
            .arg(colour, title, body);
    }

    // --- Spans ---------------------------------------------------------------

    int enterSpan(MD_SPANTYPE type, void* detail)
    {
        switch (type) {
        case MD_SPAN_EM:
            out() += QStringLiteral("<i>");
            break;
        case MD_SPAN_STRONG:
            out() += QStringLiteral("<b>");
            break;
        case MD_SPAN_U:
            out() += QStringLiteral("<u>");
            break;
        case MD_SPAN_DEL:
            out() += QStringLiteral("<s>");
            break;
        case MD_SPAN_CODE:
            out() += QStringLiteral("<code>");
            ++m_codeDepth;
            break;
        case MD_SPAN_LATEXMATH:
        case MD_SPAN_LATEXMATH_DISPLAY:
            out() += QStringLiteral("<code class=\"math\">");
            ++m_codeDepth;
            break;
        case MD_SPAN_A: {
            const auto* a = static_cast<MD_SPAN_A_DETAIL*>(detail);
            out() += QStringLiteral("<a href=\"%1\">").arg(HtmlRenderer::escape(linkUrl(attribute(a->href))));
            ++m_linkDepth;
            break;
        }
        case MD_SPAN_IMG: {
            // The alt text arrives as text; collect it and write <img> on leave.
            const auto* img = static_cast<MD_SPAN_IMG_DETAIL*>(detail);
            m_images.push_back(attribute(img->src));
            m_out.emplace_back();
            break;
        }
        case MD_SPAN_WIKILINK: {
            const auto* w = static_cast<MD_SPAN_WIKILINK_DETAIL*>(detail);
            out() += QStringLiteral("<a href=\"coeus-wiki:%1\">").arg(HtmlRenderer::escape(wikiUrl(attribute(w->target))));
            ++m_linkDepth;
            break;
        }
        }
        return 0;
    }

    int leaveSpan(MD_SPANTYPE type, void*)
    {
        switch (type) {
        case MD_SPAN_EM:
            out() += QStringLiteral("</i>");
            break;
        case MD_SPAN_STRONG:
            out() += QStringLiteral("</b>");
            break;
        case MD_SPAN_U:
            out() += QStringLiteral("</u>");
            break;
        case MD_SPAN_DEL:
            out() += QStringLiteral("</s>");
            break;
        case MD_SPAN_CODE:
        case MD_SPAN_LATEXMATH:
        case MD_SPAN_LATEXMATH_DISPLAY:
            out() += QStringLiteral("</code>");
            --m_codeDepth;
            break;
        case MD_SPAN_A:
        case MD_SPAN_WIKILINK:
            out() += QStringLiteral("</a>");
            --m_linkDepth;
            break;
        case MD_SPAN_IMG: {
            const QString alt = std::move(m_out.back());
            m_out.pop_back();
            const QString src = m_images.back();
            m_images.pop_back();
            out() += image(src, alt, false);
            break;
        }
        }
        return 0;
    }

    QString image(const QString& target, const QString& altHtml, bool wiki) const
    {
        const QString resolved = m_options.resolveImage ? m_options.resolveImage(target, wiki) : QString();
        if (resolved.isEmpty())
            return QStringLiteral("<span class=\"missing\">[%1]</span>").arg(HtmlRenderer::escape(target));
        static const QSet<QString> kPictures{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("gif"),
                                             QStringLiteral("svg"), QStringLiteral("webp"), QStringLiteral("bmp")};
        const QString suffix = QFileInfo(target).suffix().toLower();
        if (!kPictures.contains(suffix)) {
            // A PDF, a recording, a canvas: a card that opens it (SPEC.md §Reading mode).
            static const QSet<QString> kSound{QStringLiteral("mp3"), QStringLiteral("wav"), QStringLiteral("ogg"), QStringLiteral("m4a")};
            static const QSet<QString> kFilm{QStringLiteral("mp4"), QStringLiteral("webm"), QStringLiteral("mov")};
            // Words, not emoji: the reading view's fonts may have none.
            const QString mark = suffix == QLatin1String("pdf") ? QStringLiteral("[PDF]")
                                 : kSound.contains(suffix)      ? QStringLiteral("[audio]")
                                 : kFilm.contains(suffix)       ? QStringLiteral("[video]")
                                                                : QStringLiteral("[canvas]");
            return QStringLiteral("<a class=\"attachment\" href=\"%1\">%2 %3</a>")
                .arg(HtmlRenderer::escape(resolved), mark, HtmlRenderer::escape(QFileInfo(target).fileName()));
        }
        return QStringLiteral("<img src=\"%1\" alt=\"%2\"/>").arg(HtmlRenderer::escape(resolved), altHtml);
    }

    static QString linkUrl(const QString& href)
    {
        static const QRegularExpression kScheme(QStringLiteral(R"(^[A-Za-z][A-Za-z0-9+.-]*:)"));
        if (kScheme.match(href).hasMatch() || href.startsWith(QLatin1String("//")) || href.startsWith(QLatin1Char('#')))
            return href;
        return QStringLiteral("coeus-file:") + href;
    }

    static QString wikiUrl(const QString& target)
    {
        const qsizetype hash = target.indexOf(QLatin1Char('#'));
        if (hash < 0)
            return encodeTarget(target);
        return encodeTarget(target.left(hash)) + QLatin1Char('#') + encodeTarget(target.mid(hash + 1));
    }

    // --- Text ----------------------------------------------------------------

    int text(MD_TEXTTYPE type, const MD_CHAR* data, MD_SIZE size)
    {
        const QString chunk = QString::fromUtf8(data, qsizetype(size));
        if (m_codeBlock) {
            m_codeText += type == MD_TEXT_BR || type == MD_TEXT_SOFTBR ? QStringLiteral("\n")
                          : type == MD_TEXT_NULLCHAR                   ? QString(QChar(0xFFFD))
                                                                       : chunk;
            return 0;
        }
        switch (type) {
        case MD_TEXT_NULLCHAR:
            out() += QChar(0xFFFD);
            break;
        case MD_TEXT_BR:
            out() += QStringLiteral("<br/>");
            break;
        case MD_TEXT_SOFTBR:
            // A line break inside a paragraph shows as one, as in Obsidian.
            out() += m_codeDepth > 0 ? QStringLiteral("\n") : QStringLiteral("<br/>");
            break;
        case MD_TEXT_ENTITY:
            out() += chunk; // already HTML
            break;
        case MD_TEXT_NORMAL:
            out() += m_codeDepth > 0 || m_linkDepth > 0 || !m_images.empty() ? HtmlRenderer::escape(chunk)
                                                                             : decorate(chunk);
            break;
        default: // code, HTML, maths: verbatim, escaped
            out() += HtmlRenderer::escape(chunk);
            break;
        }
        return 0;
    }

    // Plain prose: ![[embeds]], ==highlights== and #tags, which md4c leaves as text.
    QString decorate(const QString& chunk) const
    {
        static const QRegularExpression kPattern(QStringLiteral(
            R"((!\[\[([^\[\]|\n]+)(?:\|([^\[\]\n]*))?\]\])|(==(?=\S)(.+?)(?<=\S)==)|((?<![\w#])#([\p{L}\p{N}_/-]+)))"));
        QString html;
        qsizetype at = 0;
        for (auto it = kPattern.globalMatch(chunk); it.hasNext();) {
            const auto m = it.next();
            html += HtmlRenderer::escape(chunk.mid(at, m.capturedStart() - at));
            at = m.capturedEnd();
            if (m.capturedLength(1) > 0) {
                html += embed(m.captured(2).trimmed(), m.captured(3).trimmed());
            } else if (m.capturedLength(4) > 0) {
                html += QStringLiteral("<span class=\"highlight\">%1</span>").arg(HtmlRenderer::escape(m.captured(5)));
            } else {
                QString name = m.captured(7);
                while (name.endsWith(QLatin1Char('/')))
                    name.chop(1);
                const bool tag = std::any_of(name.begin(), name.end(), [](QChar c) { return !c.isDigit(); });
                html += tag ? QStringLiteral("<a class=\"tag\" href=\"coeus-tag:%1\">#%2</a>")
                                  .arg(HtmlRenderer::escape(encodeTarget(name)), HtmlRenderer::escape(m.captured(7)))
                            : HtmlRenderer::escape(m.captured(0));
            }
        }
        html += HtmlRenderer::escape(chunk.mid(at));
        return html;
    }

    // Attachments render inline as images or cards; a note, or one heading's
    // section of it, is rendered in a framed block (transclusion, F-023).
    QString embed(const QString& target, const QString& alias) const
    {
        if (isAttachmentTarget(target))
            return image(target, HtmlRenderer::escape(alias.isEmpty() ? target : alias), true);
        const QString label = alias.isEmpty() ? target : alias;
        const auto asLink = [&](const QString& why) {
            QString link = QStringLiteral("<a class=\"embed\" href=\"coeus-wiki:%1\">↪ %2</a>")
                               .arg(HtmlRenderer::escape(wikiUrl(target)), HtmlRenderer::escape(label));
            if (!why.isEmpty())
                link += QStringLiteral(" <span class=\"missing\">(%1)</span>").arg(HtmlRenderer::escape(why));
            return link;
        };
        const qsizetype hash = target.indexOf(QLatin1Char('#'));
        const QString name = hash < 0 ? target : target.left(hash).trimmed();
        const QString heading = hash < 0 ? QString() : target.mid(hash + 1).trimmed();
        if (!m_options.resolveNote || heading.startsWith(QLatin1Char('^')))
            return asLink({}); // block embeds arrive with F-024
        if (m_depth >= HtmlRenderer::kEmbedDepth)
            return asLink(QStringLiteral("embedded too deeply"));
        const HtmlRenderer::Embedded note = m_options.resolveNote(name, m_sourcePath);
        if (!note.problem.isEmpty())
            return asLink(note.problem);
        if (m_ancestors.contains(note.absolutePath))
            return asLink(QStringLiteral("it embeds itself"));
        QByteArray text;
        if (heading.isEmpty()) {
            text = Transclusion::body(note.text);
        } else if (const auto section = Transclusion::section(note.text, heading)) {
            text = *section;
        } else {
            return asLink(QStringLiteral("no heading “%1”").arg(heading));
        }
        const QString inner = Writer(text, m_options, note.absolutePath, m_depth + 1, m_ancestors + QStringList{note.absolutePath}).run();
        const QString title = heading.isEmpty() ? note.title : note.title + QStringLiteral(" › ") + heading;
        const QString block =
            QStringLiteral("<table class=\"embed\" width=\"100%\" cellspacing=\"0\" cellpadding=\"8\"><tr>"
                           "<td style=\"border-left: 3px solid %1;\"><p class=\"embed-title\"><a href=\"coeus-wiki:%2\">↪ %3</a></p>"
                           "%4</td></tr></table>")
                .arg(HtmlRenderer::escape(m_options.embedAccent.isEmpty() ? QStringLiteral("#1f7a70") : m_options.embedAccent),
                     HtmlRenderer::escape(wikiUrl(target)),
                     HtmlRenderer::escape(alias.isEmpty() ? title : alias), inner);
        // A block cannot sit inside a paragraph: close it around the embed.
        return m_paragraphDepth > 0 ? QStringLiteral("</p>") + block + QStringLiteral("<p>") : block;
    }

    QByteArrayView m_body;
    const HtmlRenderer::Options& m_options;
    std::vector<QString> m_out; // stack: quotes and image alt text render into their own buffer
    std::vector<QString> m_images;
    int m_codeDepth = 0;
    int m_linkDepth = 0;
    int m_paragraphDepth = 0;
    bool m_codeBlock = false;
    QString m_codeLanguage;
    QString m_codeText;
    QString m_sourcePath;
    int m_depth = 0;
    QStringList m_ancestors; // notes being embedded around this one, outermost first
};

QString propertiesTable(const ParsedNote& note)
{
    if (note.properties.isEmpty())
        return {};
    QString html = QStringLiteral("<table class=\"properties\" cellspacing=\"0\" cellpadding=\"3\">");
    for (const Property& p : note.properties) {
        html += QStringLiteral("<tr><td class=\"key\">%1</td><td>%2</td></tr>")
                    .arg(HtmlRenderer::escape(p.key), HtmlRenderer::escape(p.values.join(QStringLiteral(", "))));
    }
    return html + QStringLiteral("</table>");
}

} // namespace

QString HtmlRenderer::escape(const QString& text)
{
    return text.toHtmlEscaped();
}

QString HtmlRenderer::render(QByteArrayView utf8, const Options& options)
{
    const Frontmatter::Split split = Frontmatter::split(utf8);
    QString html;
    if (split.present) {
        ParsedNote note;
        Frontmatter::parse(split, note);
        html += note.frontmatterError
                    ? QStringLiteral("<p class=\"problem\">Frontmatter is not valid YAML: %1</p>")
                          .arg(escape(note.frontmatterError->message))
                    : propertiesTable(note);
    }
    const QStringList self = options.notePath.isEmpty() ? QStringList() : QStringList{options.notePath};
    QString body = Writer(split.body, options, options.notePath, 0, self).run();
    body.replace(QStringLiteral("<p></p>"), QString()); // left by an embed that stood alone in its paragraph
    return html + body;
}

} // namespace coeus
