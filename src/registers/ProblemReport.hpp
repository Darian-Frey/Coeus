#pragma once

#include "index/IndexQueries.hpp"

#include <QDate>
#include <QList>
#include <QString>

#include <optional>

namespace coeus {

// The register lint's problems (F-016) chosen and written up for someone
// else to act on (F-037): the Problems panel's Copy Report and MCP's
// `problems` choose them the same way. Pure.
class ProblemReport {
public:
    // 0 error, 1 warning, 2 info.
    static int rank(const QString& level);
    // The deepest project whose folder contains `absolutePath`.
    static std::optional<ProjectRef> projectOf(const QList<ProjectRef>& projects, const QString& absolutePath);
    static QString folderOf(const ProjectRef& project); // absolute

    // The problems of `project` (not of projects nested in it), or of every
    // project if none, at most as minor as `worstRank`, in their order.
    static QList<ProblemRef> select(const QList<ProblemRef>& problems, const QList<ProjectRef>& projects,
                                    const std::optional<ProjectRef>& project, int worstRank);

    // `problems` as Markdown, grouped by file with `path:line` references:
    // relative to the project's folder for one project, else "<root>/<path>"
    // ("<path>" in the vault). Ends with what each rule present means.
    static QString markdown(const QList<ProblemRef>& problems, const std::optional<ProjectRef>& project,
                            const QString& projectLabel, const QDate& today);
    // Where a problem is, as markdown() writes it, without the line.
    static QString location(const ProblemRef& problem, const std::optional<ProjectRef>& project);
    // What a rule checks, in a sentence.
    static QString ruleMeaning(const QString& rule);
};

} // namespace coeus
