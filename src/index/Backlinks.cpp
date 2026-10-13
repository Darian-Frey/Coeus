#include "index/Backlinks.hpp"

#include "index/Database.hpp"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <cstdint>

namespace coeus {

namespace {

QString display(const QString& rootName, const QString& path)
{
    return rootName == QLatin1String("vault") ? path : rootName + QLatin1Char('/') + path;
}

QString readText(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

// "a phrase" for FTS5, with embedded quotes doubled.
QString ftsPhrase(const QString& term)
{
    QString escaped = term;
    escaped.replace(QLatin1Char('"'), QLatin1String("\"\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

} // namespace

QString Backlinks::context(const QString& lineText, qsizetype column, qsizetype length)
{
    qsizetype lead = 0;
    while (lead < lineText.size() && lineText[lead].isSpace())
        ++lead;
    const QString trimmed = lineText.trimmed();
    const qsizetype start = std::max<qsizetype>(0, column - lead);
    if (trimmed.size() <= kContextChars)
        return trimmed;
    // Keep the match visible, with room either side.
    qsizetype from = std::max<qsizetype>(0, start - (kContextChars - length) / 2);
    from = std::min(from, trimmed.size() - kContextChars);
    QString cut = trimmed.mid(from, kContextChars);
    if (from > 0)
        cut.prepend(QStringLiteral("…"));
    if (from + kContextChars < trimmed.size())
        cut.append(QStringLiteral("…"));
    return cut;
}

QList<Backlinks::Hit> Backlinks::findMentions(const QString& text, const QStringList& terms)
{
    QStringList patterns;
    for (const QString& term : terms) {
        if (term.size() >= kMinTermLength)
            patterns.append(QRegularExpression::escape(term));
    }
    if (patterns.isEmpty())
        return {};
    // Whole words: not glued to letters or digits on either side.
    const QRegularExpression termPattern(QStringLiteral("(?<![\\p{L}\\p{N}_])(%1)(?![\\p{L}\\p{N}_])").arg(patterns.join(QLatin1Char('|'))),
                                         QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kMasked(QStringLiteral(R"(!?\[\[[^\]\n]*\]\]|!?\[[^\]\n]*\]\([^)\n]*\)|`[^`\n]*`)"));
    static const QRegularExpression kFence(QStringLiteral(R"(^\s{0,3}(`{3,}|~{3,}))"));

    QList<Hit> hits;
    const QStringList lines = text.split(QLatin1Char('\n'));
    bool inFence = false;
    bool inFrontmatter = !lines.isEmpty() && lines.front() == QLatin1String("---");
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QString& line = lines[i];
        if (inFrontmatter) {
            if (i > 0 && (line == QLatin1String("---") || line == QLatin1String("...")))
                inFrontmatter = false;
            continue;
        }
        if (kFence.match(line).hasMatch()) {
            inFence = !inFence;
            continue;
        }
        if (inFence)
            continue;
        // Blank out links and code spans, keeping columns, then look for terms.
        QString masked = line;
        for (auto it = kMasked.globalMatch(line); it.hasNext();) {
            const auto m = it.next();
            masked.replace(m.capturedStart(), m.capturedLength(), QString(m.capturedLength(), QLatin1Char(' ')));
        }
        if (const auto m = termPattern.match(masked); m.hasMatch())
            hits.append({int(i + 1), line, m.capturedStart(), m.capturedLength()});
    }
    return hits;
}

std::expected<Backlinks::Result, QString> Backlinks::collect(const QString& indexPath, const QString& notePath)
{
    if (!QFileInfo::exists(indexPath))
        return std::unexpected(QStringLiteral("The index is not built yet"));
    auto db = Database::open(indexPath, Database::Mode::ReadOnly);
    if (!db)
        return std::unexpected(db.error());

    auto note = db->prepare("SELECT n.id, n.title FROM notes n JOIN roots r ON r.id = n.root_id "
                            "WHERE r.path || '/' || n.path = ?");
    if (!note)
        return std::unexpected(note.error());
    note->bind(1, notePath);
    const auto found = note->step();
    if (!found)
        return std::unexpected(found.error());
    if (!*found)
        return Result{}; // not indexed (yet): nothing links to it
    const std::int64_t noteId = note->int64(0);
    QStringList terms{note->text(1)};

    Result result;

    // Links the index resolved to this note, with the line they sit on.
    auto links = db->prepare("SELECT r.name, r.path, s.path, l.line, l.embed FROM links l "
                             "JOIN notes s ON s.id = l.src JOIN roots r ON r.id = s.root_id "
                             "WHERE l.target_note = ? AND l.src != ? ORDER BY r.id, s.path, l.line");
    if (!links)
        return std::unexpected(links.error());
    links->bind(1, noteId).bind(2, noteId);
    QHash<QString, QStringList> fileLines;
    for (;;) {
        const auto row = links->step();
        if (!row)
            return std::unexpected(row.error());
        if (!*row)
            break;
        const QString source = links->text(1) + QLatin1Char('/') + links->text(2);
        if (!fileLines.contains(source))
            fileLines.insert(source, readText(source).split(QLatin1Char('\n')));
        const QStringList& lines = fileLines[source];
        const int line = int(links->int64(3));
        const QString lineText = line >= 1 && line <= lines.size() ? lines[line - 1] : QString();
        result.links.append({source, display(links->text(0), links->text(2)), line,
                             context(lineText, 0, 0), links->int64(4) != 0});
    }

    // Unlinked mentions: full-text search narrows the notes, a line scan finds them.
    auto aliases = db->prepare("SELECT value FROM props WHERE note = ? AND key IN ('aliases', 'alias') AND value IS NOT NULL");
    if (!aliases)
        return std::unexpected(aliases.error());
    aliases->bind(1, noteId);
    while (aliases->step().value_or(false))
        terms.append(aliases->text(0));

    QStringList phrases;
    for (const QString& term : std::as_const(terms)) {
        if (term.size() >= kMinTermLength)
            phrases.append(ftsPhrase(term));
    }
    if (phrases.isEmpty())
        return result;

    auto candidates = db->prepare("SELECT r.name, r.path, n.path FROM fts JOIN notes n ON n.id = fts.rowid "
                                  "JOIN roots r ON r.id = n.root_id "
                                  "WHERE fts MATCH ? AND n.id != ? ORDER BY r.id, n.path LIMIT ?");
    if (!candidates)
        return std::unexpected(candidates.error());
    candidates->bind(1, QStringLiteral("body : (%1)").arg(phrases.join(QStringLiteral(" OR "))))
        .bind(2, noteId)
        .bind(3, kMaxMentionNotes);
    for (;;) {
        const auto row = candidates->step();
        if (!row)
            return std::unexpected(row.error());
        if (!*row)
            break;
        const QString source = candidates->text(1) + QLatin1Char('/') + candidates->text(2);
        const QString text = readText(source);
        for (const Hit& hit : findMentions(text, terms))
            result.mentions.append({source, display(candidates->text(0), candidates->text(2)), hit.line,
                                    context(hit.lineText, hit.column, hit.length), false});
    }
    return result;
}

} // namespace coeus
