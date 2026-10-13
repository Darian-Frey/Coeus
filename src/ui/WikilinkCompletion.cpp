#include "ui/WikilinkCompletion.hpp"

#include "ui/FuzzyMatch.hpp"

#include <QHash>

#include <algorithm>

namespace coeus {

namespace {

QString withoutExtension(const QString& path)
{
    return path.endsWith(QLatin1String(".md"), Qt::CaseInsensitive) ? path.chopped(3) : path;
}

QString folderOf(const QString& path)
{
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? QString() : path.left(slash);
}

int sharedFolders(const QString& a, const QString& b)
{
    const QStringList x = a.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const QStringList y = b.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    int n = 0;
    while (n < x.size() && n < y.size() && x[n] == y[n])
        ++n;
    return n;
}

QString detailOf(const NoteEntry& note)
{
    const QString folder = folderOf(note.path);
    const QString root = note.rootName == QLatin1String("vault") ? QString() : note.rootName;
    if (root.isEmpty())
        return folder;
    return folder.isEmpty() ? root : root + QLatin1Char('/') + folder;
}

QString linkTextFor(const NoteEntry& note, qsizetype sharing, const QString& sourceRoot)
{
    if (sharing <= 1)
        return note.title;
    const QString path = withoutExtension(note.path);
    return note.rootName == sourceRoot ? path : note.rootName + QLatin1Char(':') + path;
}

} // namespace

QString WikilinkCompletion::linkText(const NoteEntry& note, const QList<NoteEntry>& notes, const QString& sourceRoot)
{
    const auto sharing = std::count_if(notes.begin(), notes.end(),
                                       [&note](const NoteEntry& other) { return other.title == note.title; });
    return linkTextFor(note, sharing, sourceRoot);
}

QList<Completion> WikilinkCompletion::rank(const QString& query, const QList<NoteEntry>& notes,
                                           const QString& sourceRoot, const QString& sourceFolder)
{
    struct Scored {
        Completion completion;
        int score;
        bool sameRoot;
        int nearness;
        qsizetype length;
    };
    std::vector<Scored> scored;

    // Counted once per call: linkText per note would be quadratic.
    QHash<QString, qsizetype> titleCount;
    for (const NoteEntry& note : notes)
        ++titleCount[note.title];

    for (const NoteEntry& note : notes) {
        const auto titleScore = fuzzyScore(query, note.title);
        std::vector<std::pair<QString, int>> aliasScores;
        for (const QString& alias : note.aliases) {
            if (const auto score = fuzzyScore(query, alias))
                aliasScores.emplace_back(alias, *score);
        }
        if (!titleScore && aliasScores.empty())
            continue;

        const bool sameRoot = note.rootName == sourceRoot;
        const int nearness = sameRoot ? sharedFolders(folderOf(note.path), sourceFolder) : -1;
        const QString link = linkTextFor(note, titleCount.value(note.title), sourceRoot);
        if (titleScore)
            scored.push_back({{note.title, detailOf(note), link}, *titleScore, sameRoot, nearness, note.title.size()});
        for (const auto& [alias, score] : aliasScores)
            scored.push_back({{alias, QStringLiteral("→ %1").arg(note.title), link + QLatin1Char('|') + alias},
                              score, sameRoot, nearness, alias.size()});
    }

    std::stable_sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
        if (a.score != b.score)
            return a.score > b.score;
        if (a.sameRoot != b.sameRoot)
            return a.sameRoot;
        if (a.nearness != b.nearness)
            return a.nearness > b.nearness;
        if (a.length != b.length)
            return a.length < b.length;
        return a.completion.label.localeAwareCompare(b.completion.label) < 0;
    });

    QList<Completion> result;
    for (const Scored& s : scored) {
        if (result.size() == kMaxResults)
            break;
        result.append(s.completion);
    }
    return result;
}

QList<Completion> WikilinkCompletion::headings(const QString& query, const QStringList& headings)
{
    QList<Completion> result;
    QHash<QString, bool> seen;
    for (const QString& heading : headings) {
        if (seen.contains(heading) || !fuzzyScore(query, heading))
            continue;
        seen.insert(heading, true);
        result.append({heading, QStringLiteral("heading"), heading});
        if (result.size() == kMaxResults)
            break;
    }
    return result;
}

} // namespace coeus
