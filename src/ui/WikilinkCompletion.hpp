#pragma once

#include "index/IndexQueries.hpp"

#include <QList>
#include <QString>
#include <QStringList>

namespace coeus {

struct Completion {
    QString label;   // what the popup shows first
    QString detail;  // where the note lives
    QString insert;  // text placed between [[ and ]]
};

// Ranking and link text for the [[ completer (F-004). Pure: no index, no UI.
class WikilinkCompletion {
public:
    static constexpr int kMaxResults = 50;

    // Notes and aliases matching `query` (fuzzy, as the command palette),
    // best first. Ties prefer the source's root, then the nearest folder,
    // then the shorter title.
    static QList<Completion> rank(const QString& query, const QList<NoteEntry>& notes, const QString& sourceRoot,
                                  const QString& sourceFolder);

    // The link text that resolves to `note` from a note in `sourceRoot`
    // (SPEC.md §Wikilink resolution): the bare title when no other note shares
    // it, else the root-relative path, or root:path from another root.
    static QString linkText(const NoteEntry& note, const QList<NoteEntry>& notes, const QString& sourceRoot);

    // Headings of a note matching `query`, as `#Heading` completions.
    static QList<Completion> headings(const QString& query, const QStringList& headings);
};

} // namespace coeus
