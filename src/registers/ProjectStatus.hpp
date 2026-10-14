#pragma once

#include <QString>

#include <optional>

namespace coeus {

// Where a project stands, derived from its own documents and never kept by
// hand (F-032): the ROADMAP's phases and the CHANGELOG's [Unreleased].
// Pure: it parses text; finding the files is the caller's.
class ProjectStatus {
public:
    struct Build {
        bool hasRoadmap = false;
        int phases = 0;            // "## Phase …" sections
        int phasesComplete = 0;    // whose Status opens with "Complete"
        QString phase;             // the current one: the first not Complete, e.g. "Phase 3"
        QString phaseTitle;        // "Differentiators"
        QString phaseStatus;       // its Status line as written; empty if it has none

        bool hasChangelog = false;
        int unreleased = 0;        // top-level bullets under [Unreleased]
        QString lastRelease;       // the first released version below it, e.g. "1.0.0"
        QString lastReleaseDate;   // its date as written, if any

        // "Phase 3 — Differentiators · In progress (2 of 6 complete)", "All 5 phases complete", or "".
        QString summary() const;
    };

    // Reads `roadmap` (a ROADMAP.md) and `changelog` (a CHANGELOG.md); either may be absent.
    static Build parse(const std::optional<QString>& roadmap, const std::optional<QString>& changelog);
    // The project's `name` document: in its folder, else in docs/ or Docs/.
    static std::optional<QString> find(const QString& projectFolder, const QString& name);
};

} // namespace coeus
