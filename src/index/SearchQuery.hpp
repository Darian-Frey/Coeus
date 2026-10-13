#pragma once

#include <QString>
#include <QStringList>

namespace coeus {

// A parsed search (SPEC.md §Full-text query syntax). Pure.
//   term        full-text, matched as a prefix ("arch" finds "architecture")
//   "a phrase"  exact phrase
//   -term       exclude notes matching term or "-a phrase"
//   tag:x       notes tagged x or beneath it, any case
//   path:x      display path starts with x ("projects/", "pontus/")
//   file:x      title contains x, any case
//   re:/x/      body matches the regular expression x
// Everything is ANDed.
struct SearchQuery {
    QStringList terms;    // bare words
    QStringList phrases;  // quoted
    QStringList excluded; // words or phrases after '-'
    QStringList tags;
    QStringList paths;
    QStringList files;
    QStringList patterns; // re: bodies
    QString error;        // set when the query cannot run as written

    bool isEmpty() const;
    bool hasFullText() const { return !terms.isEmpty() || !phrases.isEmpty(); }
    // Words to look for when opening a hit, in order of preference.
    QStringList highlightWords() const;

    static SearchQuery parse(const QString& text);

    // The FTS5 expression for terms and phrases ("" when there are none).
    QString ftsExpression() const;
    static QString ftsString(const QString& text); // "text" with quotes doubled
};

} // namespace coeus
