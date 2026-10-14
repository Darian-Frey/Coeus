#pragma once

#include "index/Database.hpp"
#include "search/Embedder.hpp"

#include <QByteArray>
#include <QDeadlineTimer>
#include <QList>
#include <QString>
#include <QVector>

#include <expected>

namespace coeus {

// The index's vectors (F-018, SPEC.md §Embeddings): one per distinct passage,
// keyed by the passage's hash, scanned in full for each query; a few
// thousand 384-float vectors take well under a millisecond (D-008). Shared by
// the window's embedding worker and `coeus --mcp`. Blocking: worker threads only.
class VectorStore {
public:
    struct Coverage {
        int embedded = 0; // distinct passages with a vector
        int total = 0;    // distinct passages
        bool complete() const { return embedded >= total; }
    };

    // Makes the index's vectors those of `embedder`: if meta names another
    // model or dimension, every vector is dropped, so spaces never mix (AV-007).
    static std::expected<void, QString> adopt(Database& db, const Embedder& embedder);
    static std::expected<Coverage, QString> coverage(Database& db);
    // Embeds up to `limit` passages that have no vector yet; how many it did.
    static std::expected<int, QString> embedPending(Database& db, Embedder& embedder, int limit);
    // Embeds missing passages until none remain or `deadline` passes; how many it did.
    static std::expected<int, QString> catchUp(Database& db, Embedder& embedder, const QDeadlineTimer& deadline);
    // Drops vectors no passage uses any more; how many.
    static std::expected<int, QString> prune(Database& db);

    static QByteArray pack(const QVector<float>& vector);
    static QVector<float> unpack(const QByteArray& blob);
};

// Finding notes by meaning (F-018): the passages nearest a query vector,
// best passage per note, and a blend with the word search.
class SemanticSearch {
public:
    struct Hit {
        QString notePath; // absolute
        QString title;
        QString heading;  // the passage's section
        QString display;  // "<path>" in the vault, "<root>/<path>" in a linked root, as Search::Hit
        int startLine = 0;
        int endLine = 0;
        QString snippet;  // the start of the passage
        float score = 0;  // cosine similarity, -1…1
    };

    // The `limit` notes whose best passage is nearest `query` (a unit vector), best first.
    static std::expected<QList<Hit>, QString> nearest(const QString& indexPath, const QVector<float>& query, int limit);

    struct Blended {
        QString notePath;
        QString title;
        QString snippet;
        QString heading;  // from the meaning side, if it found the note
        int line = 0;
        bool byWords = false;
        bool byMeaning = false;
        double score = 0; // reciprocal-rank fusion
    };
    // Word hits and meaning hits merged by reciprocal-rank fusion (k = 60):
    // a note both find rises above one only one finds.
    static QList<Blended> blend(const QList<QPair<QString, QPair<QString, QString>>>& wordHits, // path -> (title, snippet)
                                const QList<Hit>& meaningHits, int limit);
};

} // namespace coeus
