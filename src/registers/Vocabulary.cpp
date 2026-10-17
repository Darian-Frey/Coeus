#include "registers/Vocabulary.hpp"

#include <QHash>
#include <QRegularExpression>

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

std::optional<QStringList> Vocabulary::forRegisterField(const QString& kind, const QString& field)
{
    using L = QStringList;
    static const QHash<QString, QStringList> kFields{
        {QStringLiteral("F/Status"), L{"Not started", "In progress", "Complete", "Withdrawn"}},
        {QStringLiteral("F/Priority"), L{"Must", "Should", "Could", "Won't"}},
        {QStringLiteral("C/Status"), L{"Proposed", "Supported", "Refuted", "Withdrawn"}},
        {QStringLiteral("C/Domain"), L{"Empirical", "Theoretical", "Mathematical"}},
        {QStringLiteral("D/Status"), L{"Proposed", "Accepted", "Superseded by D-NNN", "Deprecated"}},
        {QStringLiteral("AV/Severity"), L{"Critical", "Major", "Minor"}},
        {QStringLiteral("BUG/Status"), L{"open", "fixed", "wontfix", "deferred"}},
        {QStringLiteral("BUG/Severity"), L{"low", "medium", "high"}},
        {QStringLiteral("IMP/Status"), L{"suggested", "applied", "declined", "deferred"}},
        {QStringLiteral("IMP/Effort"), L{"trivial", "small", "medium", "large"}},
    };
    const auto it = kFields.constFind(kind + QLatin1Char('/') + field);
    if (it == kFields.constEnd())
        return std::nullopt;
    return *it;
}

std::optional<QString> Vocabulary::registerTerm(const QString& kind, const QString& field, const QString& value)
{
    const auto allowed = forRegisterField(kind, field);
    if (!allowed)
        return value.section(QLatin1Char('\n'), 0, 0).trimmed();
    return termOf(*allowed, value);
}

std::optional<QString> Vocabulary::termOf(const QStringList& allowed, const QString& value)
{
    const QString first = value.section(QLatin1Char('\n'), 0, 0).trimmed();
    static const QRegularExpression kSuperseded(QStringLiteral(R"(^Superseded by D-\d+(?![\w-]))"));
    for (const QString& term : allowed) {
        if (term == QLatin1String("Superseded by D-NNN")) {
            if (kSuperseded.match(first).hasMatch())
                return QStringLiteral("Superseded");
            continue;
        }
        // The term, then the end or a character that cannot continue a word.
        if (first.startsWith(term)
            && (first.size() == term.size() || !(first[term.size()].isLetterOrNumber() || first[term.size()] == u'_')))
            return term;
    }
    return std::nullopt;
}

bool Vocabulary::isStaleStatus(const QString& term)
{
    static const QStringList kStale{QStringLiteral("Superseded"), QStringLiteral("Withdrawn"), QStringLiteral("Refuted"),
                                    QStringLiteral("Deprecated"), QStringLiteral("wontfix"), QStringLiteral("declined")};
    return kStale.contains(term);
}

} // namespace coeus
