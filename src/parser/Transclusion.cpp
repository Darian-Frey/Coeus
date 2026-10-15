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

} // namespace coeus
