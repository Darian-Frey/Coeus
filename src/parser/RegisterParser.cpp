#include "parser/RegisterParser.hpp"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace coeus {

namespace {

const QHash<QString, QString>& fileKinds()
{
    static const QHash<QString, QString> kKinds{
        {QStringLiteral("FEATURES.md"), QStringLiteral("F")},  {QStringLiteral("CLAIMS.md"), QStringLiteral("C")},
        {QStringLiteral("DECISIONS.md"), QStringLiteral("D")}, {QStringLiteral("ATTACK_VECTORS.md"), QStringLiteral("AV")},
        {QStringLiteral("BUGS.md"), QStringLiteral("BUG")},    {QStringLiteral("IMPROVEMENTS.md"), QStringLiteral("IMP")},
    };
    return kKinds;
}

QString strip(QString line)
{
    if (line.endsWith(QLatin1Char('\r')))
        line.chop(1);
    return line;
}

bool isRelatedField(const QString& name)
{
    return name == QLatin1String("Related") || name.startsWith(QLatin1String("Related "));
}

} // namespace

bool RegisterParser::isRegisterFile(const QString& fileName)
{
    return fileKinds().contains(fileName);
}

QString RegisterParser::kindOfFile(const QString& fileName)
{
    return fileKinds().value(fileName);
}

QString RegisterParser::canonical(const QString& text)
{
    static const QRegularExpression kIdent(QStringLiteral(R"(^(F|C|D|AV|BUG|IMP)-(\d+)$)"));
    const auto m = kIdent.match(text.trimmed());
    if (!m.hasMatch())
        return {};
    return QStringLiteral("%1-%2").arg(m.captured(1)).arg(m.captured(2).toInt(), 3, 10, QLatin1Char('0'));
}

std::optional<StatusHeader> RegisterParser::statusHeader(QStringView text)
{
    static const QRegularExpression kLine(QStringLiteral(R"(^> \*\*(Status|Provenance|Last reviewed|Why this status):\*\* (.+)$)"));
    static const char* const kOrder[] = {"Status", "Provenance", "Last reviewed", "Why this status"};
    const QStringList lines = text.toString().split(QLatin1Char('\n'));
    if (lines.size() < 4)
        return std::nullopt;
    QStringList values;
    for (int i = 0; i < 4; ++i) {
        const auto m = kLine.match(strip(lines[i]));
        if (!m.hasMatch() || m.captured(1) != QLatin1String(kOrder[i]))
            return std::nullopt;
        values.append(m.captured(2).trimmed());
    }
    return StatusHeader{values[0], values[1], values[2], values[3]};
}

QList<RegisterEntry> RegisterParser::parse(QStringView text)
{
    static const QRegularExpression kEntry(QStringLiteral(R"(^### (F|C|D|AV|BUG|IMP)-(\d{3,})(:)? (.+)$)"));
    static const QRegularExpression kSection(QStringLiteral(R"(^#{2,3} )"));
    // Hyphens allowed: the standard's IMP entries carry **Trade-offs:**.
    static const QRegularExpression kSingle(QStringLiteral(R"(^\*\*([A-Za-z][A-Za-z -]*):\*\*(?: (.*))?$)"));
    static const QRegularExpression kParagraph(QStringLiteral(R"(^\*\*([A-Za-z][A-Za-z -]*)\.\*\*(?: (.*))?$)"));
    static const QRegularExpression kIdent(QStringLiteral(R"((?<![\w-])(F|C|D|AV|BUG|IMP)-(\d+)(?!\d))"));
    static const QRegularExpression kFence(QStringLiteral(R"(^\s{0,3}(`{3,}|~{3,}))"));
    static const QRegularExpression kMention(QStringLiteral(R"((?<![\w-])(F|C|D|AV|BUG|IMP)-(\d{3,})(?!\w))"));
    auto mention = [](RegisterEntry& entry, const QString& text, int line) {
        for (auto it = kMention.globalMatch(text); it.hasNext();) {
            const auto m = it.next();
            const QString ident = QStringLiteral("%1-%2").arg(m.captured(1)).arg(m.captured(2).toInt(), 3, 10, QLatin1Char('0'));
            const bool seen = std::any_of(entry.mentions.cbegin(), entry.mentions.cend(),
                                          [&ident](const auto& known) { return known.first == ident; });
            if (ident != entry.ident && !seen)
                entry.mentions.append({ident, line});
        }
    };

    QList<RegisterEntry> entries;
    RegisterEntry* current = nullptr;
    int paragraph = -1; // index into current->fields being continued, or -1
    bool inFence = false;

    const QStringList lines = text.toString().split(QLatin1Char('\n'));
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QString line = strip(lines[i]);
        if (kFence.match(line).hasMatch())
            inFence = !inFence;
        if (inFence)
            continue;

        if (const auto heading = kEntry.match(line); heading.hasMatch()) {
            RegisterEntry entry;
            entry.kind = heading.captured(1);
            entry.number = heading.captured(2).toInt();
            entry.ident = QStringLiteral("%1-%2").arg(entry.kind).arg(entry.number, 3, 10, QLatin1Char('0'));
            entry.title = heading.captured(4).trimmed();
            entry.line = int(i + 1);
            entries.append(entry);
            current = &entries.back();
            mention(*current, current->title, current->line);
            paragraph = -1;
            continue;
        }
        if (kSection.match(line).hasMatch()) { // any other level-2 or level-3 heading ends the entry
            current = nullptr;
            paragraph = -1;
            continue;
        }
        if (!current)
            continue;
        mention(*current, line, int(i + 1));

        const auto single = kSingle.match(line);
        const auto para = single.hasMatch() ? QRegularExpressionMatch() : kParagraph.match(line);
        if (single.hasMatch() || para.hasMatch()) {
            const auto& m = single.hasMatch() ? single : para;
            current->fields.append({m.captured(1).trimmed(), m.captured(2).trimmed(), int(i + 1), int(i + 1)});
            paragraph = para.hasMatch() ? int(current->fields.size() - 1) : -1;
            continue;
        }
        if (line.trimmed().isEmpty()) {
            paragraph = -1; // a blank line ends a paragraph field
            continue;
        }
        if (paragraph >= 0) {
            RegisterField& field = current->fields[paragraph];
            QString& value = field.value;
            if (value.isEmpty())
                field.valueLine = int(i + 1);
            value += (value.isEmpty() ? QString() : QStringLiteral("\n")) + line.trimmed();
        }
    }

    for (RegisterEntry& entry : entries) {
        QSet<QString> seen;
        for (const RegisterField& field : std::as_const(entry.fields)) {
            if (field.name == QLatin1String("Status") && entry.status.isEmpty())
                entry.status = field.value.section(QLatin1Char('\n'), 0, 0).trimmed();
            if (!isRelatedField(field.name))
                continue;
            // A paragraph field's value joins its lines with '\n', one per source line.
            const QStringList valueLines = field.value.split(QLatin1Char('\n'));
            for (qsizetype l = 0; l < valueLines.size(); ++l) {
                for (auto it = kIdent.globalMatch(valueLines[l]); it.hasNext();) {
                    const auto m = it.next();
                    const QString ident = QStringLiteral("%1-%2").arg(m.captured(1)).arg(m.captured(2).toInt(), 3, 10, QLatin1Char('0'));
                    if (ident != entry.ident && !seen.contains(ident)) {
                        seen.insert(ident);
                        entry.references.append(ident);
                        entry.referenceLines.append(field.valueLine + int(l));
                    }
                }
            }
        }
    }
    return entries;
}

} // namespace coeus
