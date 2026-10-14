#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

namespace coeus {

// Splits a note into the passages semantic search embeds (F-018, SPEC.md
// §Embeddings): at level-1 and level-2 headings outside fenced code (and, in
// a register file, at each entry's `### D-008 …` heading, so a match names
// the entry), then
// into windows of at most `chunkTokens` with `overlapTokens` repeated
// between neighbours. Tokens are estimated from words (a word is about 4/3
// of a token), so chunking needs no model and gives the same chunks on any
// machine. Pure.
class Chunker {
public:
    // Raised whenever the rules change, so indexes chunked the old way are
    // chunked again (1: headings; 2: register entries too).
    static constexpr int kVersion = 2;
    static constexpr int kDefaultTokens = 384;
    static constexpr int kDefaultOverlap = 48;

    struct Chunk {
        QString heading;   // "Title" or "Title › Section" for a level-2 under a level-1; "" before any heading
        int startLine = 0; // 1-based, in the file
        int endLine = 0;
        QString text;      // the passage as embedded: "<note title> › <heading>\n<words>"
        QByteArray hash;   // ContentHash of `text`: vectors are keyed by it
    };

    static QList<Chunk> split(const QString& noteText, const QString& title, int chunkTokens = kDefaultTokens,
                              int overlapTokens = kDefaultOverlap, bool registerEntries = false);
    // What an index records to know how its chunks were made.
    static QString signature(int chunkTokens, int overlapTokens)
    {
        return QStringLiteral("%1:%2:%3").arg(kVersion).arg(chunkTokens).arg(overlapTokens);
    }
    // Words per window for a token budget.
    static int wordsFor(int tokens);
};

} // namespace coeus
