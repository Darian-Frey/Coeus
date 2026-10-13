#include "registers/Vocabulary.hpp"

namespace coeus {

const QStringList& Vocabulary::projectStatus()
{
    static const QStringList kValues{QStringLiteral("Active"), QStringLiteral("Dormant"), QStringLiteral("Complete"),
                                     QStringLiteral("Archived"), QStringLiteral("Superseded")};
    return kValues;
}

const QStringList& Vocabulary::ideaStage()
{
    static const QStringList kValues{QStringLiteral("idea"), QStringLiteral("scoped"), QStringLiteral("building"),
                                     QStringLiteral("shipped"), QStringLiteral("parked")};
    return kValues;
}

std::optional<QStringList> Vocabulary::forProperty(const QString& key)
{
    if (key == QLatin1String("status"))
        return projectStatus();
    if (key == QLatin1String("stage"))
        return ideaStage();
    return std::nullopt;
}

} // namespace coeus
