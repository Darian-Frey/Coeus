#pragma once

#include "index/SearchQuery.hpp"

#include <QList>
#include <QString>

#include <expected>

namespace coeus {

// Full-text search over the index (F-006, SPEC.md §Full-text query syntax).
// One read-only connection and one SQL statement per search; meant to run
// off the UI thread.
class Search {
public:
    struct Hit {
        QString notePath;    // absolute
        QString noteDisplay; // "<path>" in the vault, "<root>/<path>" in a linked root
        QString title;
        QString snippet;     // body excerpt; matches between kMarkStart and kMarkEnd
    };

    static constexpr int kDefaultLimit = 200;
    static constexpr QChar kMarkStart{0x2};
    static constexpr QChar kMarkEnd{0x3};

    // Hits best first: by relevance (title above body) when the query has
    // words or phrases, else by display path.
    static std::expected<QList<Hit>, QString> run(const QString& indexPath, const SearchQuery& query,
                                                  int limit = kDefaultLimit);
};

} // namespace coeus
