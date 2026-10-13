#pragma once

#include <QDate>
#include <QString>
#include <QStringList>

namespace coeus {

// The project-scaffold standard's skeletons, for "Insert next ID" and for a
// register file that does not exist yet (SPEC.md §ID allocation). Pure.
class RegisterTemplates {
public:
    // The register file holding `kind`: "D" -> "DECISIONS.md".
    static QString fileName(const QString& kind);
    // A human name: "D" -> "Decisions".
    static QString displayName(const QString& kind);
    // The opening of a new register file.
    static QString fileSkeleton(const QString& kind);
    // A new entry, starting "### <ident> Title" (or "### <ident>: Title"
    // for BUG and IMP), ending with a blank line. `kTitle` marks the title.
    static QString entry(const QString& kind, const QString& ident, const QDate& today);

    static constexpr const char* kTitle = "Title";
    static const QStringList& kinds(); // F C D AV BUG IMP
};

} // namespace coeus
