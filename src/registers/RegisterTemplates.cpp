#include "registers/RegisterTemplates.hpp"

#include <QStringList>

namespace coeus {

const QStringList& RegisterTemplates::kinds()
{
    static const QStringList kKinds{QStringLiteral("F"), QStringLiteral("C"), QStringLiteral("D"),
                                    QStringLiteral("AV"), QStringLiteral("BUG"), QStringLiteral("IMP")};
    return kKinds;
}

QString RegisterTemplates::fileName(const QString& kind)
{
    if (kind == QLatin1String("F"))
        return QStringLiteral("FEATURES.md");
    if (kind == QLatin1String("C"))
        return QStringLiteral("CLAIMS.md");
    if (kind == QLatin1String("D"))
        return QStringLiteral("DECISIONS.md");
    if (kind == QLatin1String("AV"))
        return QStringLiteral("ATTACK_VECTORS.md");
    if (kind == QLatin1String("BUG"))
        return QStringLiteral("BUGS.md");
    if (kind == QLatin1String("IMP"))
        return QStringLiteral("IMPROVEMENTS.md");
    return {};
}

QString RegisterTemplates::displayName(const QString& kind)
{
    if (kind == QLatin1String("F"))
        return QStringLiteral("Features");
    if (kind == QLatin1String("C"))
        return QStringLiteral("Claims");
    if (kind == QLatin1String("D"))
        return QStringLiteral("Decisions");
    if (kind == QLatin1String("AV"))
        return QStringLiteral("Attack vectors");
    if (kind == QLatin1String("BUG"))
        return QStringLiteral("Bugs");
    if (kind == QLatin1String("IMP"))
        return QStringLiteral("Improvements");
    return kind;
}

QString RegisterTemplates::fileSkeleton(const QString& kind)
{
    if (kind == QLatin1String("F"))
        return QStringLiteral("# Features\n\n## Features\n\n");
    if (kind == QLatin1String("C"))
        return QStringLiteral("# Claims\n\nEmpirical, theoretical, or mathematical assertions made by this project, with "
                              "falsification conditions.\n\nStatus vocabulary: Proposed | Supported | Refuted | Withdrawn.\n\n"
                              "## Claims\n\n");
    if (kind == QLatin1String("D"))
        return QStringLiteral("# Decisions\n\nAppend-only log. Status: Proposed | Accepted | Superseded by D-NNN | Deprecated.\n\n");
    if (kind == QLatin1String("AV"))
        return QStringLiteral("# Attack Vectors\n\nProject-specific failure modes the project must be resilient against.\n"
                              "Severity: Critical | Major | Minor.\n\n");
    if (kind == QLatin1String("BUG"))
        return QStringLiteral("# Bugs\n\nStatus vocabulary: open | fixed | wontfix | deferred.\n"
                              "Severity vocabulary: low | medium | high.\n\n## Open\n\n");
    if (kind == QLatin1String("IMP"))
        return QStringLiteral("# Improvements\n\nStatus vocabulary: suggested | applied | declined | deferred.\n"
                              "Effort vocabulary: trivial | small | medium | large.\n\n## Suggested\n\n");
    return {};
}

QString RegisterTemplates::entry(const QString& kind, const QString& ident, const QDate& today)
{
    const QString date = today.toString(Qt::ISODate);
    if (kind == QLatin1String("F"))
        return QStringLiteral("### %1 Title\n**Priority:** Should\n**Acceptance:**\n- \n**Status:** Not started\n\n").arg(ident);
    if (kind == QLatin1String("C"))
        return QStringLiteral("### %1 Title\n**Status:** Proposed\n**Domain:** Empirical\n**Authors:** \n**Related:** \n\n"
                              "**Statement.** \n\n**Falsification conditions.** \n\n**Current evidence.** \n\n"
                              "**Reversal conditions.** \n\n")
            .arg(ident);
    if (kind == QLatin1String("D"))
        return QStringLiteral("### %1 Title\n**Decided:** %2\n**Recorded:** %2\n**Status:** Proposed\n**Authors:** \n"
                              "**Related:** \n\n**Context.** \n\n**Options.**\n- **A. ** \n- **B. ** \n\n**Decision.** \n\n"
                              "**Consequences.**\n- \n\n**Reversal conditions.** \n\n")
            .arg(ident, date);
    if (kind == QLatin1String("AV"))
        return QStringLiteral("### %1 Title\n**Severity:** Major\n**Description.** \n**Detection.** \n**Related decisions.** \n"
                              "**History.** Identified %2.\n\n")
            .arg(ident, date);
    if (kind == QLatin1String("BUG"))
        return QStringLiteral("### %1: Title\n**Status:** open\n**Severity:** medium\n**Found:** %2\n**Location:** \n"
                              "**Description.** \n**Reproduction.** \n**Notes.** \n\n")
            .arg(ident, date);
    if (kind == QLatin1String("IMP"))
        return QStringLiteral("### %1: Title\n**Status:** suggested\n**Found:** %2\n**Location:** \n**Effort:** small\n"
                              "**Description.** \n**Proposal.** \n**Trade-offs.** \n**Notes.** \n\n")
            .arg(ident, date);
    return {};
}

} // namespace coeus
