#pragma once

#include "git/GitRepository.hpp"
#include "index/IndexQueries.hpp"
#include "registers/ProjectStatus.hpp"

#include <QDate>

#include <optional>

namespace coeus {

// Everything the project board and `project_status` (F-032, F-033) say
// about one project, read from its folder's documents and its repository.
// Blocking (it reads files and Git): never on the UI thread (AV-005).
struct ProjectOverview {
    ProjectRef project;
    QString label;            // "projects/coeus" in the vault, "pontus" or "pontus/sub" in a linked root
    QString folder;           // absolute
    QString readme;           // absolute
    int reviewedDaysAgo = -1; // -1: Last reviewed is not a date
    ProjectStatus::Build build;
    bool inGit = false;
    QString workdir;          // the repository's working tree, if in Git
    QString branch;
    std::optional<GitRepository::Tracking> tracking;
    int changes = 0;          // changed files under the project's folder

    static ProjectOverview of(const ProjectRef& project, const QDate& today);
    static QString labelOf(const ProjectRef& project);
};

} // namespace coeus
