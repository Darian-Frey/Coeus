#pragma once

#include <QDate>
#include <QString>

class QTextStream;

namespace coeus {

// `coeus --lint <folder>` (IMP-004): the register lint (F-016) from a
// terminal, for the pre-commit hook and ctest. Indexes the folder, honouring
// its Git ignore rules, into a temporary index (the vault's own is never
// touched), and prints every problem as `path:line: level: message (rule)`.
// Exit status: 0 clean, 1 errors (or warnings with `strict`, or no project
// to lint), 2 the folder could not be linted.
class LintCommand {
public:
    struct Options {
        bool strict = false;   // warnings fail too
        bool quiet = false;    // print errors only
        bool mentions = false; // also: IDs mentioned anywhere in a project's notes that it does not define
        QDate today = QDate::currentDate();
    };

    static int run(const QString& folder, const Options& options, QTextStream& out, QTextStream& err);
};

} // namespace coeus
