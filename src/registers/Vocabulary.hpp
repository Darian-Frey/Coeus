#pragma once

#include <QString>
#include <QStringList>

#include <optional>

namespace coeus {

// The fixed vocabularies of SPEC.md §Vocabularies and §Ideas, in one place
// for the register lint (F-016) and, for ideas, the properties panel (F-008,
// through vault/PropertyVocabulary, D-025). Values are case-sensitive.
class Vocabulary {
public:
    static const QStringList& projectStatus(); // Active, Dormant, Complete, Archived, Superseded
    static const QStringList& ideaStage();     // idea, scoped, building, shipped, parked

    // The allowed values of a register field (`kind` F, C, D, AV, BUG, IMP;
    // `field` Status, Priority, Domain, Severity, Effort), if it has a vocabulary.
    // D's "Superseded by D-NNN" is listed as written.
    static std::optional<QStringList> forRegisterField(const QString& kind, const QString& field);
    // The vocabulary term `value` opens with, or nullopt if it opens with none.
    // A term may be followed by a qualifier after a non-word character, as the
    // standard's registers write them: "fixed (2026-10-05)", "Complete (Phase 2)",
    // "Superseded by D-019 (2026-10-06): …" (term "Superseded"). Only the first
    // line counts. Fields with no vocabulary return the first line unchanged.
    static std::optional<QString> registerTerm(const QString& kind, const QString& field, const QString& value);
    // The same match against any list of allowed values (the project statuses).
    static std::optional<QString> termOf(const QStringList& allowed, const QString& value);
    // Statuses that make an entry a stale citation target (SPEC.md L2).
    static bool isStaleStatus(const QString& term);
};

} // namespace coeus
