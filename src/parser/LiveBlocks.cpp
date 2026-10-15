#include "parser/LiveBlocks.hpp"

#include "parser/Attachments.hpp"

#include <QRegularExpression>
#include <QStringList>

namespace coeus {

QList<LiveBlock> LiveBlocks::scan(const QString& text)
{
    static const QRegularExpression kFence(QStringLiteral(R"(^ {0,3}(`{3,}|~{3,})\s*([^\s`]*))"));
    static const QRegularExpression kWikiPicture(QStringLiteral(R"(^\s*!\[\[([^\[\]|\n]+?)(?:\|[^\[\]\n]*)?\]\]\s*$)"));
    static const QRegularExpression kMarkdownPicture(QStringLiteral(R"(^\s*!\[[^\]\n]*\]\(([^)\s]+)\)\s*$)"));
    static const QRegularExpression kDelimiterRow(QStringLiteral(R"(^\s*\|?\s*:?-+:?\s*(\|\s*:?-+:?\s*)*\|?\s*$)"));
    static const QRegularExpression kCallout(QStringLiteral(R"(^ {0,3}>\s?\[![A-Za-z]+\])"));
    static const QRegularExpression kQuoteLine(QStringLiteral(R"(^ {0,3}>)"));
    const QStringList lines = text.split(QLatin1Char('\n'));
    QList<LiveBlock> blocks;
    qsizetype i = 0;

    // Frontmatter: "---" on the first line to the next "---" or "...".
    if (!lines.isEmpty() && lines.front().trimmed() == QLatin1String("---")) {
        for (qsizetype j = 1; j < lines.size(); ++j) {
            const QString t = lines[j].trimmed();
            if (t == QLatin1String("---") || t == QLatin1String("...")) {
                blocks.append({LiveBlock::Kind::Frontmatter, 0, int(j), {}, {}, false});
                i = j + 1;
                break;
            }
        }
    }

    while (i < lines.size()) {
        const QString& line = lines[i];
        const QString trimmed = line.trimmed();
        if (const auto fence = kFence.match(line); fence.hasMatch()) {
            const QString marker = fence.captured(1);
            const QString language = fence.captured(2);
            qsizetype end = i + 1;
            while (end < lines.size()) {
                const QString closing = lines[end].trimmed();
                if (closing.size() >= marker.size() && closing.startsWith(marker.at(0))
                    && closing.count(marker.at(0)) == closing.size())
                    break;
                ++end;
            }
            const qsizetype last = std::min(end, lines.size() - 1);
            const bool mermaid = language.compare(QLatin1String("mermaid"), Qt::CaseInsensitive) == 0;
            LiveBlock block{mermaid ? LiveBlock::Kind::Mermaid : LiveBlock::Kind::Code, int(i), int(last), language, {}, false};
            if (mermaid)
                block.source = lines.mid(i + 1, std::max<qsizetype>(0, end - i - 1)).join(QLatin1Char('\n'));
            blocks.append(block);
            i = last + 1;
            continue;
        }
        if (trimmed.startsWith(QLatin1String("$$"))) {
            // On one line, or to the line that ends with $$.
            qsizetype end = i;
            if (!(trimmed.size() > 4 && trimmed.endsWith(QLatin1String("$$")))) {
                end = i + 1;
                while (end < lines.size() && !lines[end].trimmed().endsWith(QLatin1String("$$")))
                    ++end;
            }
            if (end < lines.size()) {
                QString source = lines.mid(i, end - i + 1).join(QLatin1Char('\n')).trimmed();
                source = source.mid(2, source.size() - 4).trimmed();
                if (!source.isEmpty())
                    blocks.append({LiveBlock::Kind::Maths, int(i), int(end), {}, source, false});
                i = end + 1;
                continue;
            }
        }
        // A table: a header row, then a delimiter row, then rows with pipes.
        if (line.contains(QLatin1Char('|')) && i + 1 < lines.size() && lines[i + 1].contains(QLatin1Char('-'))
            && kDelimiterRow.match(lines[i + 1]).hasMatch()) {
            qsizetype end = i + 2;
            while (end < lines.size() && lines[end].contains(QLatin1Char('|')) && !lines[end].trimmed().isEmpty())
                ++end;
            blocks.append({LiveBlock::Kind::Table, int(i), int(end - 1), {}, lines.mid(i, end - i).join(QLatin1Char('\n')), false});
            i = end;
            continue;
        }
        // A callout: its head and every quote line after it.
        if (kCallout.match(line).hasMatch()) {
            qsizetype end = i + 1;
            while (end < lines.size() && kQuoteLine.match(lines[end]).hasMatch())
                ++end;
            blocks.append({LiveBlock::Kind::Callout, int(i), int(end - 1), {}, lines.mid(i, end - i).join(QLatin1Char('\n')), false});
            i = end;
            continue;
        }
        if (const auto m = kWikiPicture.match(line); m.hasMatch() && !isAttachmentTarget(m.captured(1).section(QLatin1Char('#'), 0, 0))) {
            blocks.append({LiveBlock::Kind::Embed, int(i), int(i), {}, line.trimmed(), true}); // as written, alias and all
        } else if (m.hasMatch()) {
            blocks.append({LiveBlock::Kind::Picture, int(i), int(i), {}, m.captured(1).trimmed(), true});
        } else if (const auto md = kMarkdownPicture.match(line); md.hasMatch() && isAttachmentTarget(md.captured(1).section(QLatin1Char('#'), 0, 0))) {
            blocks.append({LiveBlock::Kind::Picture, int(i), int(i), {}, md.captured(1), false});
        }
        ++i;
    }
    return blocks;
}

} // namespace coeus
