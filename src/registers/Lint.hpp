#pragma once

#include "parser/RegisterParser.hpp"

#include <QDate>
#include <QList>
#include <QPair>
#include <QString>

#include <cstdint>
#include <optional>
#include <tuple>

namespace coeus {

// The register lint (F-016, SPEC.md §Lint rules), as pure functions over
// what the index holds. LintResolver feeds it from index.db and stores the
// result; L8 depends on today's date, so it is evaluated when read instead.
class Lint {
public:
    struct Citation {
        QString ident;            // canonical, e.g. "D-005"
        int line = 0;             // the line citing it
        std::int64_t target = 0;  // the entry it resolved to, 0 if none
    };

    struct Entry {
        std::int64_t id = 0;
        std::int64_t note = 0;
        QString notePath;         // root-relative, for messages
        std::int64_t project = 0; // 0: outside every project, never linted
        QString kind;
        QString ident;
        int line = 0;
        QList<RegisterField> fields;
        QList<Citation> citations;            // Related fields (L1, L2)
        QList<QPair<QString, int>> mentions;  // anywhere in the entry, with lines (L4)
    };

    struct Project {
        std::int64_t id = 0;
        std::int64_t note = 0; // the README carrying the status header
        QString status;
        QString lastReviewed;
    };

    struct Problem {
        std::int64_t note = 0;
        QString rule;    // "L1" … "L9"
        QString level;   // error, warning, info
        int line = 0;
        QString message;
        QString subject; // text on the line to underline, e.g. the cited ID

        bool operator==(const Problem&) const = default;
        bool operator<(const Problem& o) const
        {
            return std::tie(note, line, rule, subject, message, level)
                   < std::tie(o.note, o.line, o.rule, o.subject, o.message, o.level);
        }
    };

    // The status header's line numbers (SPEC.md §Status header).
    static constexpr int kStatusLine = 1;
    static constexpr int kLastReviewedLine = 3;
    static constexpr int kDefaultReviewThresholdDays = 14;

    // Whether an entry of kind `from` mentioning one of kind `to` must be
    // mentioned back: Maintenance Rule 6 of the standard, and rule 4 of its
    // BUGS and IMPROVEMENTS sections (as tools/check_xrefs.py).
    static bool needsBackReference(const QString& from, const QString& to);

    // L1–L7 and L9 over every entry and project, sorted.
    static QList<Problem> check(const QList<Entry>& entries, const QList<Project>& projects);
    // L8: the project's Last reviewed is older than `thresholdDays`, or not a date.
    static std::optional<Problem> staleReview(const Project& project, const QDate& today, int thresholdDays);
};

} // namespace coeus
