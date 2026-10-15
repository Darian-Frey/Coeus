#include "parser/Transclusion.hpp"

#include "parser/Frontmatter.hpp"

#include <QList>
#include <QRegularExpression>

namespace coeus {

namespace {

// Each line with its heading level (0 if not a heading), fences honoured.
struct Line {
    QByteArray text;
    int level = 0;
    QString heading;
};

QList<Line> lines(QByteArrayView body)
{
    static const QRegularExpression kHeading(QStringLiteral(R"(^ {0,3}(#{1,6})[ \t]+(.*?)[ \t]*#*[ \t]*$)"));
    static const QRegularExpression kFence(QStringLiteral(R"(^ {0,3}(`{3,}|~{3,}))"));
    QList<Line> result;
    QString fence;
    for (const QByteArray& raw : QByteArray(body.data(), body.size()).split('\n')) {
        Line line{raw, 0, {}};
        const QString text = QString::fromUtf8(raw);
        if (const auto f = kFence.match(text); f.hasMatch()) {
            const QString marker = f.captured(1);
            if (fence.isEmpty())
                fence = marker;
            else if (marker.at(0) == fence.at(0) && marker.size() >= fence.size())
                fence.clear();
        } else if (fence.isEmpty()) {
            if (const auto h = kHeading.match(text); h.hasMatch()) {
                line.level = int(h.captured(1).size());
                line.heading = h.captured(2).trimmed();
            }
        }
        result.append(line);
    }
    return result;
}

} // namespace

QByteArray Transclusion::body(QByteArrayView note)
{
    const Frontmatter::Split split = Frontmatter::split(note);
    return QByteArray(split.body.data(), split.body.size());
}

std::optional<QByteArray> Transclusion::section(QByteArrayView note, const QString& heading)
{
    const QByteArray text = body(note);
    const QList<Line> all = lines(text);
    const QString wanted = heading.trimmed();
    for (qsizetype i = 0; i < all.size(); ++i) {
        if (all[i].level == 0 || all[i].heading.compare(wanted, Qt::CaseInsensitive) != 0)
            continue;
        QByteArray section = all[i].text;
        for (qsizetype j = i + 1; j < all.size() && !(all[j].level > 0 && all[j].level <= all[i].level); ++j)
            section += '\n' + all[j].text;
        return section;
    }
    return std::nullopt;
}

namespace {

const QRegularExpression& blockIdPattern()
{
    static const QRegularExpression kBlockId(QStringLiteral(R"((?:^|\s)\^([A-Za-z0-9-]+)\s*$)"));
    return kBlockId;
}

// The note's blocks, IDs or not, by its own lines (frontmatter skipped).
QList<Transclusion::Block> allBlocks(QByteArrayView note)
{
    static const QRegularExpression kListItem(QStringLiteral(R"(^\s*(?:[-*+]|\d+[.)])\s+)"));
    const Frontmatter::Split split = Frontmatter::split(note);
    const int offset = split.bodyFirstLine - 1; // lines before the body
    const QList<Line> body = lines(split.body);
    QList<Transclusion::Block> result;
    std::optional<Transclusion::Block> open;
    const auto close = [&] {
        if (!open)
            return;
        const QString last = QString::fromUtf8(open->text.mid(open->text.lastIndexOf('\n') + 1));
        if (const auto m = blockIdPattern().match(last); m.hasMatch()) {
            open->id = m.captured(1);
            open->text.chop(QString(m.captured(0)).toUtf8().size());
            while (open->text.endsWith(' ') || open->text.endsWith('\t'))
                open->text.chop(1);
        }
        result.append(*open);
        open.reset();
    };
    bool inFence = false;
    static const QRegularExpression kFence(QStringLiteral(R"(^ {0,3}(`{3,}|~{3,}))"));
    for (qsizetype i = 0; i < body.size(); ++i) {
        const int number = offset + int(i) + 1;
        const QString text = QString::fromUtf8(body[i].text);
        if (kFence.match(text).hasMatch()) {
            close();
            inFence = !inFence;
            continue;
        }
        if (inFence || text.trimmed().isEmpty() || body[i].level > 0) {
            close();
            continue;
        }
        if (kListItem.match(text).hasMatch())
            close(); // each item is a block of its own
        if (!open)
            open = Transclusion::Block{{}, number, number, body[i].text};
        else {
            open->lastLine = number;
            open->text += '\n' + body[i].text;
        }
    }
    close();
    return result;
}

} // namespace

QList<Transclusion::Block> Transclusion::blocks(QByteArrayView note)
{
    QList<Block> named;
    for (const Block& b : allBlocks(note))
        if (!b.id.isEmpty())
            named.append(b);
    return named;
}

std::optional<Transclusion::Block> Transclusion::block(QByteArrayView note, const QString& id)
{
    for (const Block& b : allBlocks(note))
        if (b.id == id)
            return b;
    return std::nullopt;
}

std::optional<Transclusion::Block> Transclusion::blockAt(QByteArrayView note, int line)
{
    for (const Block& b : allBlocks(note))
        if (line >= b.firstLine && line <= b.lastLine)
            return b;
    return std::nullopt;
}

std::optional<int> Transclusion::headingLine(QByteArrayView note, const QString& heading)
{
    const Frontmatter::Split split = Frontmatter::split(note);
    const QList<Line> body = lines(split.body);
    for (qsizetype i = 0; i < body.size(); ++i)
        if (body[i].level > 0 && body[i].heading.compare(heading.trimmed(), Qt::CaseInsensitive) == 0)
            return split.bodyFirstLine + int(i);
    return std::nullopt;
}

} // namespace coeus
