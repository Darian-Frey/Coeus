#include "registers/Lint.hpp"

#include "registers/Vocabulary.hpp"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace coeus {

namespace {

const RegisterField* findField(const Lint::Entry& entry, const QString& name)
{
    for (const RegisterField& field : entry.fields) {
        if (field.name.compare(name, Qt::CaseInsensitive) == 0)
            return &field;
    }
    return nullptr;
}

QString statusTerm(const Lint::Entry& entry)
{
    const RegisterField* status = findField(entry, QStringLiteral("Status"));
    return status ? Vocabulary::registerTerm(entry.kind, QStringLiteral("Status"), status->value).value_or(QString())
                  : QString();
}

// The fields each kind must carry (L5–L7).
struct Required {
    const char* rule;
    QStringList kinds;
    const char* field;
};

} // namespace

QList<Lint::Problem> Lint::check(const QList<Entry>& entries, const QList<Project>& projects)
{
    QList<Problem> problems;
    QHash<std::int64_t, const Entry*> byId;
    for (const Entry& entry : entries)
        byId.insert(entry.id, &entry);

    // L9: an ident twice in one project. The first by path and line keeps
    // it, as citations do (SPEC.md §Registers); every later one is an error.
    QHash<QPair<std::int64_t, QString>, const Entry*> first; // the entry an ident names in a project
    {
        QHash<QPair<std::int64_t, QString>, QList<const Entry*>> byIdent;
        for (const Entry& entry : entries) {
            if (entry.project)
                byIdent[{entry.project, entry.ident}].append(&entry);
        }
        for (auto it = byIdent.cbegin(); it != byIdent.cend(); ++it) {
            QList<const Entry*> same = it.value();
            std::sort(same.begin(), same.end(), [](const Entry* a, const Entry* b) {
                return std::tie(a->notePath, a->line) < std::tie(b->notePath, b->line);
            });
            const Entry* kept = same.front();
            first.insert(it.key(), kept);
            for (qsizetype i = 1; i < same.size(); ++i) {
                problems.append({same[i]->note, QStringLiteral("L9"), QStringLiteral("error"), same[i]->line,
                                 QStringLiteral("%1 is already used at %2:%3; IDs are never reused (AV-003)")
                                     .arg(kept->ident, kept->notePath)
                                     .arg(kept->line),
                                 same[i]->ident});
            }
        }
    }

    static const QList<Required> kRequired{
        {"L5", {QStringLiteral("D"), QStringLiteral("C")}, "Reversal conditions"},
        {"L6", {QStringLiteral("IMP")}, "Trade-offs"},
        {"L7", {QStringLiteral("AV")}, "Detection"},
    };
    static const QStringList kVocabularyFields{QStringLiteral("Status"), QStringLiteral("Priority"),
                                               QStringLiteral("Domain"), QStringLiteral("Severity"),
                                               QStringLiteral("Effort")};

    for (const Entry& entry : entries) {
        if (!entry.project)
            continue; // outside every project: indexed, never linted (SPEC.md §Registers)

        // L1, L2, L4: each citation.
        for (const Citation& citation : entry.citations) {
            if (!citation.target) {
                problems.append({entry.note, QStringLiteral("L1"), QStringLiteral("error"), citation.line,
                                 QStringLiteral("%1 cites %2, which does not exist in this project").arg(entry.ident, citation.ident),
                                 citation.ident});
                continue;
            }
            const Entry* target = byId.value(citation.target);
            if (!target)
                continue;
            const QString term = statusTerm(*target);
            const RegisterField* targetStatus = findField(*target, QStringLiteral("Status"));
            // "Superseded by D-032", cited by D-032 itself, is the successor's history, not staleness.
            static const QRegularExpression kSupersededBy(QStringLiteral(R"(^Superseded by (D-\d+))"));
            const auto by = targetStatus ? kSupersededBy.match(targetStatus->value) : QRegularExpressionMatch();
            const bool supersededByCiter = by.hasMatch() && RegisterParser::canonical(by.captured(1)) == entry.ident;
            if (Vocabulary::isStaleStatus(term) && !supersededByCiter) {
                problems.append({entry.note, QStringLiteral("L2"), QStringLiteral("warning"), citation.line,
                                 QStringLiteral("%1 cites %2, which is %3").arg(entry.ident, citation.ident,
                                                                              targetStatus->value.section(QLatin1Char('\n'), 0, 0)),
                                 citation.ident});
            }
        }

        // L4: a mention the standard says must be returned, that is not.
        // A duplicate (L9) is judged once, by its first entry.
        for (const auto& [ident, line] : first.value({entry.project, entry.ident}) == &entry ? entry.mentions
                                                                                           : decltype(entry.mentions)()) {
            const Entry* target = first.value({entry.project, ident});
            if (!target || !needsBackReference(entry.kind, target->kind))
                continue;
            const bool mentionedBack = std::any_of(target->mentions.begin(), target->mentions.end(),
                                                   [&entry](const auto& m) { return m.first == entry.ident; });
            if (!mentionedBack) {
                problems.append({entry.note, QStringLiteral("L4"), QStringLiteral("warning"), line,
                                 QStringLiteral("%1 cites %2, but %2 does not cite %1").arg(entry.ident, ident), ident});
            }
        }

        // L3: vocabulary fields, each occurrence.
        for (const RegisterField& field : entry.fields) {
            if (!kVocabularyFields.contains(field.name))
                continue;
            const auto allowed = Vocabulary::forRegisterField(entry.kind, field.name);
            if (!allowed || Vocabulary::registerTerm(entry.kind, field.name, field.value))
                continue;
            const QString value = field.value.section(QLatin1Char('\n'), 0, 0).trimmed();
            problems.append({entry.note, QStringLiteral("L3"), QStringLiteral("error"), field.valueLine,
                             value.isEmpty()
                                 ? QStringLiteral("%1 %2 is empty; expected one of: %3").arg(entry.ident, field.name, allowed->join(QStringLiteral(", ")))
                                 : QStringLiteral("%1 %2 \"%3\" is not one of: %4").arg(entry.ident, field.name, value, allowed->join(QStringLiteral(", "))),
                             value});
        }

        // L5–L7: required fields.
        for (const Required& required : kRequired) {
            if (required.kinds.contains(entry.kind) && !findField(entry, QLatin1String(required.field))) {
                problems.append({entry.note, QLatin1String(required.rule), QStringLiteral("error"), entry.line,
                                 QStringLiteral("%1 has no %2 field").arg(entry.ident, QLatin1String(required.field)),
                                 entry.ident});
            }
        }
    }

    // L3 for the project status in the README's header.
    for (const Project& project : projects) {
        if (!Vocabulary::termOf(Vocabulary::projectStatus(), project.status)) {
            problems.append({project.note, QStringLiteral("L3"), QStringLiteral("error"), kStatusLine,
                             QStringLiteral("Project status \"%1\" is not one of: %2")
                                 .arg(project.status, Vocabulary::projectStatus().join(QStringLiteral(", "))),
                             project.status});
        }
    }

    std::sort(problems.begin(), problems.end());
    return problems;
}

std::optional<Lint::Problem> Lint::staleReview(const Project& project, const QDate& today, int thresholdDays)
{
    const QDate reviewed = QDate::fromString(project.lastReviewed.left(10), Qt::ISODate);
    if (!reviewed.isValid()) {
        return Problem{project.note, QStringLiteral("L8"), QStringLiteral("info"), kLastReviewedLine,
                       QStringLiteral("Last reviewed \"%1\" is not a YYYY-MM-DD date").arg(project.lastReviewed),
                       project.lastReviewed};
    }
    const qint64 age = reviewed.daysTo(today);
    if (age <= thresholdDays)
        return std::nullopt;
    return Problem{project.note, QStringLiteral("L8"), QStringLiteral("info"), kLastReviewedLine,
                   QStringLiteral("Last reviewed %1, %2 days ago (more than %3)")
                       .arg(project.lastReviewed.left(10))
                       .arg(age)
                       .arg(thresholdDays),
                   project.lastReviewed.left(10)};
}

bool Lint::needsBackReference(const QString& from, const QString& to)
{
    static const QHash<QString, QStringList> kPairs{
        {QStringLiteral("D"), {QStringLiteral("AV"), QStringLiteral("F"), QStringLiteral("C")}},
        {QStringLiteral("AV"), {QStringLiteral("D"), QStringLiteral("C")}},
        {QStringLiteral("F"), {QStringLiteral("D")}},
        {QStringLiteral("C"), {QStringLiteral("D"), QStringLiteral("AV")}},
        {QStringLiteral("BUG"), {QStringLiteral("AV"), QStringLiteral("D"), QStringLiteral("IMP"), QStringLiteral("BUG")}},
        {QStringLiteral("IMP"), {QStringLiteral("BUG"), QStringLiteral("D"), QStringLiteral("IMP")}},
    };
    return kPairs.value(from).contains(to);
}

} // namespace coeus
