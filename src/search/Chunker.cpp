#include "search/Chunker.hpp"

#include "index/ContentHash.hpp"
#include "parser/Frontmatter.hpp"

#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace coeus {

namespace {

struct Word {
    QString text;
    int line;
};

struct Section {
    QString heading;
    QList<Word> words;
};

} // namespace

int Chunker::wordsFor(int tokens)
{
    return std::max(1, tokens * 3 / 4);
}

QList<Chunker::Chunk> Chunker::split(const QString& noteText, const QString& title, int chunkTokens, int overlapTokens,
                                     bool registerEntries)
{
    static const QRegularExpression kEntry(QStringLiteral(R"(^### ((?:F|C|D|AV|BUG|IMP)-\d{3,}:? .*?)\s*$)"));
    static const QRegularExpression kHeading(QStringLiteral(R"(^ {0,3}(#{1,2})\s+(.*?)\s*#*\s*$)"));
    static const QRegularExpression kFence(QStringLiteral(R"(^\s{0,3}(`{3,}|~{3,}))"));
    static const QRegularExpression kSpace(QStringLiteral(R"(\s+)"));

    // The frontmatter is properties, not prose: skip its lines.
    const QByteArray bytes = noteText.toUtf8();
    const Frontmatter::Split split = Frontmatter::split(bytes);
    const QStringList lines = noteText.split(QLatin1Char('\n'));
    int firstLine = 0;
    if (split.present)
        firstLine = int(QString::fromUtf8(bytes.left(bytes.size() - split.body.size())).count(QLatin1Char('\n')));

    QList<Section> sections{Section{}};
    QString top; // the current level-1 heading
    bool inFence = false;
    for (int i = firstLine; i < lines.size(); ++i) {
        const QString& line = lines[i];
        if (kFence.match(line).hasMatch())
            inFence = !inFence;
        if (!inFence) {
            if (const auto m = kHeading.match(line); m.hasMatch()) {
                const QString text = m.captured(2);
                if (m.captured(1).size() == 1)
                    top = text;
                sections.append({m.captured(1).size() == 2 && !top.isEmpty() ? top + QStringLiteral(" › ") + text : text, {}});
            } else if (const auto entry = registerEntries ? kEntry.match(line) : QRegularExpressionMatch(); entry.hasMatch()) {
                sections.append({top.isEmpty() ? entry.captured(1) : top + QStringLiteral(" › ") + entry.captured(1), {}});
            }
        }
        for (const QString& word : line.split(kSpace, Qt::SkipEmptyParts))
            sections.back().words.append({word, i + 1});
    }

    const int window = wordsFor(chunkTokens);
    const int step = std::max(1, window - std::clamp(wordsFor(overlapTokens), 0, window - 1));
    QList<Chunk> chunks;
    for (const Section& section : std::as_const(sections)) {
        if (section.words.isEmpty())
            continue;
        for (qsizetype start = 0; start < section.words.size(); start += step) {
            const qsizetype end = std::min(section.words.size(), start + window);
            QString body;
            int line = section.words[start].line;
            for (qsizetype w = start; w < end; ++w) {
                if (w > start)
                    body += section.words[w].line != line ? QLatin1Char('\n') : QLatin1Char(' ');
                line = section.words[w].line;
                body += section.words[w].text;
            }
            Chunk chunk;
            chunk.heading = section.heading;
            chunk.startLine = section.words[start].line;
            chunk.endLine = section.words[end - 1].line;
            chunk.text = (section.heading.isEmpty() ? title : title + QStringLiteral(" › ") + section.heading) + QLatin1Char('\n') + body;
            chunk.hash = ContentHash::of(chunk.text.toUtf8());
            chunks.append(chunk);
            if (end == section.words.size())
                break;
        }
    }
    return chunks;
}

} // namespace coeus
