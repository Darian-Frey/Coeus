#pragma once

#include <QDate>
#include <QList>
#include <QPair>
#include <QString>

#include <optional>

namespace coeus {

// The text of F-032's two board commands, kept pure so they are testable
// without files: refreshing a README's status header, and the files that
// promoting an idea creates. Writing them is MainWindow's, through AtomicWriter.
class ProjectScaffold {
public:
    // `readme` with its header's Last reviewed set to `today`, every other
    // byte as it was; nullopt if the first four lines are not a status header.
    static std::optional<QString> withLastReviewed(const QString& readme, const QDate& today);

    // A folder name for a codename: kept as typed when it is safe (letters,
    // digits, spaces, `.`, `_`, `-`; not starting with `.`), else empty.
    static QString folderName(const QString& codename);
    // A linked-root name for a codename (SPEC.md §Linked roots: letters,
    // digits, `.`, `_`, `-`), lower-cased, spaces as `-`; empty if none is left.
    static QString rootName(const QString& codename);

    // The files a promoted idea becomes, by name, README.md first: the README
    // opens with the status header (Status: Active, reviewed `today`), then the
    // idea's summary and repo, then its body without frontmatter (the header
    // must open the file); then the standard's skeletons for FEATURES, ROADMAP,
    // DECISIONS, CLAUDE and CHANGELOG, with no entries, so no ID is allocated.
    static QList<QPair<QString, QString>> promotion(const QString& ideaText, const QString& ideaPath,
                                                    const QString& codename, const QDate& today);
};

} // namespace coeus
