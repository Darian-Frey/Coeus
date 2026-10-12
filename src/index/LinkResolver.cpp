#include "index/LinkResolver.hpp"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QSet>
#include <QStringList>

#include <cstdint>
#include <optional>

namespace coeus {

namespace {

struct NoteRef {
    std::int64_t id;
    int root;
    QString path;   // root-relative
    QString folder; // root-relative, "" at the root
};

struct Outcome {
    std::int64_t target = 0; // 0 when unresolved
    bool ambiguous = false;
};

QString folderOf(const QString& path)
{
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? QString() : path.left(slash);
}

// Number of leading folder segments two folders share.
int commonPrefix(const QString& a, const QString& b)
{
    const QStringList x = a.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const QStringList y = b.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    int n = 0;
    while (n < x.size() && n < y.size() && x[n] == y[n])
        ++n;
    return n;
}

class Index {
public:
    QHash<QPair<int, QString>, std::int64_t> byPath;  // (root, path) -> id
    QHash<QString, QList<NoteRef>> byTitle;            // title -> notes
    QHash<QString, QList<NoteRef>> byAlias;            // alias -> notes
    QHash<QString, int> rootByName;

    std::optional<std::int64_t> path(int root, const QString& path) const
    {
        const auto it = byPath.constFind({root, path});
        return it == byPath.constEnd() ? std::nullopt : std::optional(*it);
    }

    // The unique nearest candidate by shared folder prefix, or ambiguous.
    static Outcome nearest(const QList<NoteRef>& candidates, const QString& folder)
    {
        if (candidates.isEmpty())
            return {};
        int best = -1;
        int count = 0;
        std::int64_t id = 0;
        for (const NoteRef& c : candidates) {
            const int score = commonPrefix(c.folder, folder);
            if (score > best) {
                best = score;
                count = 1;
                id = c.id;
            } else if (score == best) {
                ++count;
            }
        }
        return count == 1 ? Outcome{id, false} : Outcome{0, true};
    }

    Outcome wiki(const NoteRef& source, QString target) const
    {
        if (target.endsWith(QLatin1String(".md"), Qt::CaseInsensitive))
            target.chop(3);
        const QString asPath = target + QLatin1String(".md");

        // 1. Root-relative path in the source's root; then root:path.
        if (const auto id = path(source.root, asPath))
            return {*id, false};
        if (const qsizetype colon = target.indexOf(QLatin1Char(':')); colon > 0) {
            const auto root = rootByName.constFind(target.left(colon));
            if (root != rootByName.constEnd()) {
                if (const auto id = path(*root, target.mid(colon + 1) + QLatin1String(".md")))
                    return {*id, false};
            }
        }

        // A target with folders matches notes whose path ends with it.
        const qsizetype slash = target.lastIndexOf(QLatin1Char('/'));
        const QString title = slash < 0 ? target : target.mid(slash + 1);
        const QString suffix = QLatin1Char('/') + asPath;
        QList<NoteRef> sameFolder, sameRoot, otherRoots;
        for (const NoteRef& c : byTitle.value(title)) {
            if (slash >= 0 && !(c.path == asPath || c.path.endsWith(suffix)))
                continue;
            if (c.root != source.root)
                otherRoots.append(c);
            else if (c.folder == source.folder)
                sameFolder.append(c);
            else
                sameRoot.append(c);
        }

        // 2. Same folder. 3. Same root, nearest folder. 4. Other roots.
        if (sameFolder.size() == 1)
            return {sameFolder.front().id, false};
        if (!sameRoot.isEmpty())
            return nearest(sameRoot, source.folder);
        if (!otherRoots.isEmpty())
            return nearest(otherRoots, source.folder);

        // 5. Aliases, same root first.
        QList<NoteRef> aliasSame, aliasOther;
        for (const NoteRef& c : byAlias.value(target))
            (c.root == source.root ? aliasSame : aliasOther).append(c);
        for (const QList<NoteRef>* group : {&aliasSame, &aliasOther}) {
            if (group->size() == 1)
                return {group->front().id, false};
            if (group->size() > 1)
                return {0, true};
        }
        return {}; // 6. Unresolved.
    }

    Outcome markdown(const NoteRef& source, const QString& target) const
    {
        QString resolved = target.startsWith(QLatin1Char('/'))
                               ? QDir::cleanPath(target.mid(1))
                               : QDir::cleanPath(source.folder.isEmpty() ? target : source.folder + QLatin1Char('/') + target);
        if (resolved == QLatin1String("..") || resolved.startsWith(QLatin1String("../")))
            return {};
        if (const auto id = path(source.root, resolved))
            return {*id, false};
        if (!resolved.endsWith(QLatin1String(".md"), Qt::CaseInsensitive)) {
            if (const auto id = path(source.root, resolved + QLatin1String(".md")))
                return {*id, false};
        }
        return {};
    }
};

} // namespace

bool LinkResolver::isAttachment(const QString& target)
{
    static const QSet<QString> kExtensions{
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("gif"),
        QStringLiteral("svg"), QStringLiteral("webp"), QStringLiteral("bmp"), QStringLiteral("pdf"),
        QStringLiteral("mp3"), QStringLiteral("wav"), QStringLiteral("ogg"), QStringLiteral("m4a"),
        QStringLiteral("mp4"), QStringLiteral("webm"), QStringLiteral("mov"), QStringLiteral("canvas"),
    };
    return kExtensions.contains(QFileInfo(target).suffix().toLower());
}

bool LinkResolver::isFileLink(bool markdown, const QString& target)
{
    if (isAttachment(target))
        return true;
    // A Markdown link names a file, so any extension but .md is not a note.
    // Wikilinks name titles, which may contain dots ("v1.2 notes").
    const QString suffix = QFileInfo(target).suffix();
    return markdown && !suffix.isEmpty() && suffix.compare(QLatin1String("md"), Qt::CaseInsensitive) != 0;
}

std::expected<LinkResolver::Report, QString> LinkResolver::resolveAll(Database& db)
{
    Index index;
    QHash<std::int64_t, NoteRef> notes;

    auto roots = db.prepare("SELECT id, name FROM roots");
    if (!roots)
        return std::unexpected(roots.error());
    while (true) {
        const auto row = roots->step();
        if (!row)
            return std::unexpected(row.error());
        if (!*row)
            break;
        index.rootByName.insert(roots->text(1), int(roots->int64(0)));
    }

    auto noteRows = db.prepare("SELECT id, root_id, path, title FROM notes");
    if (!noteRows)
        return std::unexpected(noteRows.error());
    while (true) {
        const auto row = noteRows->step();
        if (!row)
            return std::unexpected(row.error());
        if (!*row)
            break;
        const NoteRef ref{noteRows->int64(0), int(noteRows->int64(1)), noteRows->text(2), folderOf(noteRows->text(2))};
        notes.insert(ref.id, ref);
        index.byPath.insert({ref.root, ref.path}, ref.id);
        index.byTitle[noteRows->text(3)].append(ref);
    }

    auto aliasRows = db.prepare("SELECT note, value FROM props WHERE key IN ('aliases', 'alias') AND value IS NOT NULL");
    if (!aliasRows)
        return std::unexpected(aliasRows.error());
    while (true) {
        const auto row = aliasRows->step();
        if (!row)
            return std::unexpected(row.error());
        if (!*row)
            break;
        const auto note = notes.constFind(aliasRows->int64(0));
        if (note != notes.constEnd())
            index.byAlias[aliasRows->text(1)].append(*note);
    }

    struct Change {
        std::int64_t rowid;
        Outcome outcome;
    };
    QList<Change> changes;
    Report report;

    auto linkRows = db.prepare("SELECT rowid, src, style, target_raw, target_note, ambiguous FROM links");
    if (!linkRows)
        return std::unexpected(linkRows.error());
    while (true) {
        const auto row = linkRows->step();
        if (!row)
            return std::unexpected(row.error());
        if (!*row)
            break;
        const auto source = notes.constFind(linkRows->int64(1));
        if (source == notes.constEnd())
            continue;
        const QString target = linkRows->text(3);
        const bool markdown = linkRows->text(2) == QLatin1String("markdown");
        Outcome outcome;
        if (!isFileLink(markdown, target)) {
            outcome = markdown ? index.markdown(*source, target) : index.wiki(*source, target);
            if (outcome.target != 0)
                ++report.resolved;
            else if (outcome.ambiguous)
                ++report.ambiguous;
            else
                ++report.unresolved;
        }
        const std::int64_t current = linkRows->isNull(4) ? 0 : linkRows->int64(4);
        if (current != outcome.target || bool(linkRows->int64(5)) != outcome.ambiguous)
            changes.append({linkRows->int64(0), outcome});
    }

    auto update = db.prepare("UPDATE links SET target_note = ?, ambiguous = ? WHERE rowid = ?");
    if (!update)
        return std::unexpected(update.error());
    for (const Change& change : std::as_const(changes)) {
        update->reset();
        if (change.outcome.target != 0)
            update->bind(1, change.outcome.target);
        else
            update->bindNull(1);
        if (auto result = update->bind(2, int(change.outcome.ambiguous)).bind(3, change.rowid).run(); !result)
            return std::unexpected(result.error());
    }
    report.changed = int(changes.size());
    return report;
}

} // namespace coeus
