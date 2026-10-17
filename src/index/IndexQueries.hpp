#pragma once

#include <QList>
#include <QDate>
#include <QString>
#include <QStringList>

#include <expected>
#include <optional>

namespace coeus {

// One indexed note, as the [[ completer sees it.
struct NoteEntry {
    QString rootName;  // "vault" for root 0
    QString rootPath;  // absolute
    QString path;      // root-relative, with .md
    QString title;     // filename without extension
    QStringList aliases;
    QString project;   // root-relative folder of the note's project; empty if none
};

// One register entry, for the register panel and ID completion (F-015).
struct RegisterRef {
    QString rootName;
    QString rootPath;
    QString project;   // root-relative project folder
    QString kind;
    int number = 0;
    QString ident;
    QString title;
    QString status;
    QString notePath;  // absolute path of the register file
    int line = 0;
};

// One project, from its README status header (SPEC.md §Status header).
struct ProjectRef {
    QString rootName;
    QString rootPath;
    QString path;      // root-relative folder; "" for a project at the root
    QString status;
    QString lastReviewed;
};

// One idea (SPEC.md §Ideas, F-032).
struct IdeaRef {
    QString notePath;  // absolute
    QString title;     // the note's title
    QString stage;
    QString codename;
    QString repo;
    QString summary;
};

// One lint problem (F-016), for the Problems panel and the editor.
struct ProblemRef {
    QString rootName;
    QString rootPath;
    QString notePath;  // absolute
    QString rule;      // L1 … L9
    QString level;     // error, warning, info
    int line = 0;
    QString message;
    QString subject;   // text on the line to underline
};

// Read-only questions the UI asks of the index (ARCHITECTURE.md: the UI
// thread holds read-only connections). Each call opens and closes its own
// connection, so a Rebuild Index on the worker can never leave it pointing at
// a deleted file. Every query is a single indexed lookup.
class IndexQueries {
public:
    struct LinkTarget {
        QString absolutePath; // empty when unresolved or ambiguous
        bool ambiguous = false;
    };

    // How the index resolved the wikilink `target` written in the note at
    // `sourcePath` (SPEC.md §Wikilink resolution). nullopt if the index has no
    // such link, e.g. it has not caught up with an unsaved edit yet.
    static std::expected<std::optional<LinkTarget>, QString> wikilinkTarget(const QString& indexPath,
                                                                           const QString& sourcePath,
                                                                           const QString& target);

    // Every indexed note with its aliases, for the [[ completer (F-004).
    static std::expected<QList<NoteEntry>, QString> allNotes(const QString& indexPath);
    // Every register entry inside a project, by project then kind then number.
    static std::expected<QList<RegisterRef>, QString> registers(const QString& indexPath);
    static std::expected<QList<ProjectRef>, QString> projects(const QString& indexPath);
    // Every idea, by path.
    static std::expected<QList<IdeaRef>, QString> ideas(const QString& indexPath);
    // The values notes give the frontmatter text property `key`, most used
    // first, at most `limit` and none longer than a line: the Properties
    // card's suggestions (F-008, D-025).
    static std::expected<QStringList, QString> propertyValues(const QString& indexPath, const QString& key, int limit = 30);
    // Every stored lint problem, plus L8 (stale review) evaluated for `today`,
    // by root, note path and line.
    static std::expected<QList<ProblemRef>, QString> problems(const QString& indexPath, const QDate& today,
                                                              int reviewThresholdDays);
    // Heading texts of the note at `notePath`, in document order.
    static std::expected<QStringList, QString> headings(const QString& indexPath, const QString& notePath);
};

} // namespace coeus
