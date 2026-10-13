#pragma once

#include <QString>
#include <QStringList>

#include <optional>

namespace coeus {

// The fixed vocabularies of SPEC.md §Vocabularies and §Ideas, in one place
// for the properties panel (F-008) now and the register lint (F-016) later.
// Values are case-sensitive.
class Vocabulary {
public:
    static const QStringList& projectStatus(); // Active, Dormant, Complete, Archived, Superseded
    static const QStringList& ideaStage();     // idea, scoped, building, shipped, parked

    // The allowed values of a frontmatter key, if it is an enum (F-008):
    // status takes the project statuses, stage the idea stages.
    static std::optional<QStringList> forProperty(const QString& key);
};

} // namespace coeus
