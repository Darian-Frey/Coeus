#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <expected>

namespace coeus {

// What the backlinks panel shows for one note (F-005). Built off the UI
// thread: it reads the index and the linking notes' text.
class Backlinks {
public:
    struct Reference {
        QString sourcePath;    // absolute
        QString sourceDisplay; // "<path>" in the vault, "<root>/<path>" in a linked root
        int line = 0;          // 1-based
        QString context;       // the line, trimmed and shortened around the match
        bool embed = false;    // ![[…]] rather than [[…]]
    };

    struct Result {
        QList<Reference> links;    // resolved links to the note, from other notes
        QList<Reference> mentions; // plain-text occurrences of its title or aliases
    };

    static constexpr int kMinTermLength = 3;    // shorter titles mention everything
    static constexpr int kMaxMentionNotes = 100;
    static constexpr int kContextChars = 160;

    static std::expected<Result, QString> collect(const QString& indexPath, const QString& notePath);

    // 1-based lines of `text` where a term occurs as a whole word,
    // case-insensitively, outside links, code spans and fenced code. Pure.
    struct Hit {
        int line;
        QString lineText;
        qsizetype column;
        qsizetype length;
    };
    static QList<Hit> findMentions(const QString& text, const QStringList& terms);

    // `lineText` trimmed and cut to about kContextChars around [column, column+length).
    static QString context(const QString& lineText, qsizetype column, qsizetype length);
};

} // namespace coeus
