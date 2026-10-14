#include "registers/ProjectOverview.hpp"

#include <QFile>

namespace coeus {

namespace {

std::optional<QString> slurp(const std::optional<QString>& path)
{
    if (!path)
        return std::nullopt;
    QFile file(*path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return QString::fromUtf8(file.readAll());
}

} // namespace

QString ProjectOverview::labelOf(const ProjectRef& project)
{
    if (project.rootName == QLatin1String("vault"))
        return project.path.isEmpty() ? QStringLiteral("(vault)") : project.path;
    return project.path.isEmpty() ? project.rootName : project.rootName + QLatin1Char('/') + project.path;
}

ProjectOverview ProjectOverview::of(const ProjectRef& project, const QDate& today)
{
    ProjectOverview row;
    row.project = project;
    row.label = labelOf(project);
    row.folder = project.path.isEmpty() ? project.rootPath : project.rootPath + QLatin1Char('/') + project.path;
    row.readme = row.folder + QStringLiteral("/README.md");
    const QDate reviewed = QDate::fromString(project.lastReviewed.left(10), Qt::ISODate);
    row.reviewedDaysAgo = reviewed.isValid() ? int(reviewed.daysTo(today)) : -1;
    row.build = ProjectStatus::parse(slurp(ProjectStatus::find(row.folder, QStringLiteral("ROADMAP.md"))),
                                     slurp(ProjectStatus::find(row.folder, QStringLiteral("CHANGELOG.md"))));
    if (const auto repo = GitRepository::discover(row.folder)) {
        row.inGit = true;
        row.workdir = repo->workdir();
        row.branch = repo->branch();
        row.tracking = repo->tracking();
        const QString prefix = repo->relativePath(row.folder);
        if (const auto changes = repo->status()) {
            for (const GitChange& change : *changes) {
                if (prefix == QLatin1String(".") || prefix.isEmpty() || change.path.startsWith(prefix + QLatin1Char('/')))
                    ++row.changes;
            }
        }
    }
    return row;
}

} // namespace coeus
