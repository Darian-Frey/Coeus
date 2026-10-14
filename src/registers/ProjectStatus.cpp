#include "registers/ProjectStatus.hpp"

#include "registers/Vocabulary.hpp"

#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

namespace coeus {

namespace {

const QStringList& phaseStatuses()
{
    static const QStringList kValues{QStringLiteral("Not started"), QStringLiteral("In progress"), QStringLiteral("Complete")};
    return kValues;
}

} // namespace

QString ProjectStatus::Build::summary() const
{
    if (!hasRoadmap || phases == 0)
        return {};
    if (phase.isEmpty())
        return phases == 1 ? QStringLiteral("The one phase is complete") : QStringLiteral("All %1 phases complete").arg(phases);
    const QString status = phaseStatus.isEmpty() ? QStringLiteral("no status") : phaseStatus.section(QLatin1Char('\n'), 0, 0);
    return QStringLiteral("%1 — %2 · %3 (%4 of %5 complete)")
        .arg(phase, phaseTitle, status.length() > 60 ? status.left(59) + QStringLiteral("…") : status)
        .arg(phasesComplete)
        .arg(phases);
}

ProjectStatus::Build ProjectStatus::parse(const std::optional<QString>& roadmap, const std::optional<QString>& changelog)
{
    static const QRegularExpression kPhase(QStringLiteral(R"(^##\s+(Phase\s+\S+?)\s*[—–:-]+\s*(.+?)\s*$)"));
    static const QRegularExpression kStatus(QStringLiteral(R"(^\*\*Status:?\*\*:?\s*(.*)$)"));
    static const QRegularExpression kRelease(QStringLiteral(R"(^##\s+\[([^\]]+)\](?:\s*[-—–]\s*(\S+))?)"));
    static const QRegularExpression kFence(QStringLiteral(R"(^\s{0,3}(`{3,}|~{3,}))"));

    Build build;
    if (roadmap) {
        build.hasRoadmap = true;
        struct Phase {
            QString name, title, status;
        };
        QList<Phase> phases;
        bool inFence = false;
        bool inPhase = false;
        for (const QString& raw : roadmap->split(QLatin1Char('\n'))) {
            const QString line = raw.trimmed();
            if (kFence.match(line).hasMatch())
                inFence = !inFence;
            if (inFence)
                continue;
            if (line.startsWith(QLatin1String("## ")) || line.startsWith(QLatin1String("# "))) {
                const auto m = kPhase.match(line);
                inPhase = m.hasMatch();
                if (inPhase)
                    phases.append({m.captured(1).simplified(), m.captured(2), QString()});
                continue;
            }
            if (inPhase && phases.back().status.isEmpty()) {
                if (const auto m = kStatus.match(line); m.hasMatch())
                    phases.back().status = m.captured(1).trimmed();
            }
        }
        build.phases = int(phases.size());
        for (const Phase& phase : std::as_const(phases)) {
            const auto term = Vocabulary::termOf(phaseStatuses(), phase.status);
            if (term == QLatin1String("Complete")) {
                ++build.phasesComplete;
            } else if (build.phase.isEmpty()) {
                build.phase = phase.name;
                build.phaseTitle = phase.title;
                build.phaseStatus = phase.status;
            }
        }
    }
    if (changelog) {
        build.hasChangelog = true;
        bool inUnreleased = false;
        bool inFence = false;
        for (const QString& line : changelog->split(QLatin1Char('\n'))) {
            if (kFence.match(line).hasMatch())
                inFence = !inFence;
            if (inFence)
                continue;
            if (line.startsWith(QLatin1String("## "))) {
                const auto m = kRelease.match(line);
                if (!m.hasMatch()) {
                    inUnreleased = false;
                    continue;
                }
                if (m.captured(1).compare(QLatin1String("Unreleased"), Qt::CaseInsensitive) == 0) {
                    inUnreleased = true;
                    continue;
                }
                build.lastRelease = m.captured(1);
                build.lastReleaseDate = m.captured(2);
                break; // the newest release: nothing below matters
            }
            if (inUnreleased && (line.startsWith(QLatin1String("- ")) || line.startsWith(QLatin1String("* "))))
                ++build.unreleased;
        }
    }
    return build;
}

std::optional<QString> ProjectStatus::find(const QString& projectFolder, const QString& name)
{
    for (const QString& folder : {QString(), QStringLiteral("docs/"), QStringLiteral("Docs/")}) {
        const QString path = projectFolder + QLatin1Char('/') + folder + name;
        if (QFileInfo(path).isFile())
            return path;
    }
    return std::nullopt;
}

} // namespace coeus
