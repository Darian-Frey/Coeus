#include "registers/ProblemReport.hpp"

#include <QDir>
#include <QStringList>

namespace coeus {

int ProblemReport::rank(const QString& level)
{
    return level == QLatin1String("error") ? 0 : level == QLatin1String("warning") ? 1 : 2;
}

QString ProblemReport::folderOf(const ProjectRef& project)
{
    return project.path.isEmpty() ? project.rootPath : project.rootPath + QLatin1Char('/') + project.path;
}

std::optional<ProjectRef> ProblemReport::projectOf(const QList<ProjectRef>& projects, const QString& absolutePath)
{
    std::optional<ProjectRef> best;
    qsizetype bestLength = -1;
    for (const ProjectRef& project : projects) {
        const QString folder = folderOf(project);
        if (absolutePath.startsWith(folder + QLatin1Char('/')) && folder.size() > bestLength) {
            bestLength = folder.size();
            best = project;
        }
    }
    return best;
}

QList<ProblemRef> ProblemReport::select(const QList<ProblemRef>& problems, const QList<ProjectRef>& projects,
                                        const std::optional<ProjectRef>& project, int worstRank)
{
    QList<ProblemRef> chosen;
    const QString folder = project ? folderOf(*project) : QString();
    for (const ProblemRef& problem : problems) {
        if (rank(problem.level) > worstRank)
            continue;
        if (project) {
            const auto owner = projectOf(projects, problem.notePath);
            if (!owner || folderOf(*owner) != folder)
                continue;
        }
        chosen.append(problem);
    }
    return chosen;
}

QString ProblemReport::location(const ProblemRef& problem, const std::optional<ProjectRef>& project)
{
    if (project && problem.notePath.startsWith(folderOf(*project) + QLatin1Char('/')))
        return QDir(folderOf(*project)).relativeFilePath(problem.notePath);
    const QString inRoot = QDir(problem.rootPath).relativeFilePath(problem.notePath);
    return problem.rootName == QLatin1String("vault") ? inRoot : problem.rootName + QLatin1Char('/') + inRoot;
}

QString ProblemReport::ruleMeaning(const QString& rule)
{
    static const QHash<QString, QString> kMeanings{
        {QStringLiteral("L1"), QStringLiteral("a Related field cites an ID the project does not define")},
        {QStringLiteral("L2"), QStringLiteral("a Related field cites an entry that is superseded, withdrawn, refuted, deprecated, wontfix or declined")},
        {QStringLiteral("L3"), QStringLiteral("a Status, Priority, Domain, Severity or Effort value, or a README's status, is outside its vocabulary")},
        {QStringLiteral("L4"), QStringLiteral("an entry mentions another that the standard says must mention it back, and it does not")},
        {QStringLiteral("L5"), QStringLiteral("a D- or C- entry has no Reversal conditions field")},
        {QStringLiteral("L6"), QStringLiteral("an IMP- entry has no Trade-offs field")},
        {QStringLiteral("L7"), QStringLiteral("an AV- entry has no Detection field")},
        {QStringLiteral("L8"), QStringLiteral("the README's Last reviewed date is older than the review threshold, or not a date")},
        {QStringLiteral("L9"), QStringLiteral("an ID is used by an earlier entry in the project; IDs are never renumbered, so the later one needs a new ID")},
    };
    return kMeanings.value(rule);
}

QString ProblemReport::markdown(const QList<ProblemRef>& problems, const std::optional<ProjectRef>& project,
                                const QString& projectLabel, const QDate& today)
{
    int counts[3] = {0, 0, 0};
    QStringList rules;
    for (const ProblemRef& problem : problems) {
        ++counts[rank(problem.level)];
        if (!rules.contains(problem.rule))
            rules.append(problem.rule);
    }
    std::sort(rules.begin(), rules.end(), [](const QString& a, const QString& b) { return a.mid(1).toInt() < b.mid(1).toInt(); });

    QString out;
    out += QStringLiteral("# Register problems: %1\n\n").arg(projectLabel);
    out += QStringLiteral("%1 · %2 error(s) · %3 warning(s) · %4 info, from Coeus's register lint (F-016)")
               .arg(today.toString(Qt::ISODate))
               .arg(counts[0])
               .arg(counts[1])
               .arg(counts[2]);
    out += project ? QStringLiteral(". Paths are relative to the project folder.\n") : QStringLiteral(".\n");
    if (problems.isEmpty())
        return out + QStringLiteral("\nNo problems.\n");

    QString file;
    for (const ProblemRef& problem : problems) {
        const QString where = location(problem, project);
        if (where != file) {
            file = where;
            out += QStringLiteral("\n## %1\n\n").arg(file);
        }
        out += QStringLiteral("- `%1:%2` %3 %4: %5\n").arg(where).arg(problem.line).arg(problem.level, problem.rule, problem.message);
    }
    out += QStringLiteral("\n## Rules\n\n");
    for (const QString& rule : std::as_const(rules))
        out += QStringLiteral("- %1: %2.\n").arg(rule, ruleMeaning(rule));
    return out;
}

} // namespace coeus
