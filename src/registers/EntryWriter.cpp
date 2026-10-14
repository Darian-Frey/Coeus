#include "registers/EntryWriter.hpp"

#include "parser/RegisterParser.hpp"
#include "registers/Vocabulary.hpp"

#include <QHash>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace coeus {

namespace {

struct Layout {
    QStringList order;  // the usual order of fields
    QStringList single; // `**Name:** value` fields; the rest are `**Name.** value` paragraphs
    bool spaced;        // paragraphs separated by blank lines (D, C) or not
};

const Layout& layoutOf(const QString& kind)
{
    static const QHash<QString, Layout> kLayouts{
        {QStringLiteral("F"), {{"Priority", "Acceptance", "Status", "Notes"}, {"Priority", "Acceptance", "Status"}, false}},
        {QStringLiteral("C"),
         {{"Status", "Domain", "Authors", "Related", "Statement", "Falsification conditions", "Current evidence", "Reversal conditions"},
          {"Status", "Domain", "Authors", "Related"},
          true}},
        {QStringLiteral("D"),
         {{"Decided", "Recorded", "Status", "Authors", "Related", "Context", "Options", "Decision", "Consequences", "Reversal conditions"},
          {"Decided", "Recorded", "Status", "Authors", "Related"},
          true}},
        {QStringLiteral("AV"), {{"Severity", "Description", "Detection", "Related decisions", "History"}, {"Severity"}, false}},
        {QStringLiteral("BUG"),
         {{"Status", "Severity", "Found", "Fixed", "Location", "Description", "Reproduction", "Fix", "Related", "Notes"},
          {"Status", "Severity", "Found", "Fixed", "Location", "Related"},
          false}},
        {QStringLiteral("IMP"),
         {{"Status", "Found", "Location", "Effort", "Description", "Proposal", "Trade-offs", "Notes", "Applied", "Related"},
          {"Status", "Found", "Location", "Effort", "Related"},
          false}},
    };
    static const Layout kNone{};
    const auto it = kLayouts.constFind(kind);
    return it == kLayouts.constEnd() ? kNone : *it;
}

// Fields an entry must have, or the lint reports an error (L5–L7).
QStringList required(const QString& kind)
{
    if (kind == QLatin1String("D") || kind == QLatin1String("C"))
        return {QStringLiteral("Reversal conditions")};
    if (kind == QLatin1String("IMP"))
        return {QStringLiteral("Trade-offs")};
    if (kind == QLatin1String("AV"))
        return {QStringLiteral("Detection")};
    return {};
}

// What a new entry starts with when the caller does not say.
QList<QPair<QString, QString>> defaults(const QString& kind, const QDate& today)
{
    const QString date = today.toString(Qt::ISODate);
    if (kind == QLatin1String("D"))
        return {{QStringLiteral("Decided"), date}, {QStringLiteral("Recorded"), date}, {QStringLiteral("Status"), QStringLiteral("Proposed")}};
    if (kind == QLatin1String("F"))
        return {{QStringLiteral("Priority"), QStringLiteral("Should")}, {QStringLiteral("Status"), QStringLiteral("Not started")}};
    if (kind == QLatin1String("C"))
        return {{QStringLiteral("Status"), QStringLiteral("Proposed")}};
    if (kind == QLatin1String("BUG"))
        return {{QStringLiteral("Status"), QStringLiteral("open")}, {QStringLiteral("Found"), date}};
    if (kind == QLatin1String("IMP"))
        return {{QStringLiteral("Status"), QStringLiteral("suggested")}, {QStringLiteral("Found"), date}};
    return {};
}

// The section a BUG or IMP status belongs in.
QString sectionFor(const QString& kind, const QString& status)
{
    static const QHash<QString, QString> kSections{
        {QStringLiteral("BUG/open"), QStringLiteral("Open")},          {QStringLiteral("BUG/fixed"), QStringLiteral("Fixed")},
        {QStringLiteral("BUG/wontfix"), QStringLiteral("Won't Fix")},  {QStringLiteral("BUG/deferred"), QStringLiteral("Deferred")},
        {QStringLiteral("IMP/suggested"), QStringLiteral("Suggested")}, {QStringLiteral("IMP/applied"), QStringLiteral("Applied")},
        {QStringLiteral("IMP/declined"), QStringLiteral("Declined")},  {QStringLiteral("IMP/deferred"), QStringLiteral("Deferred")},
    };
    const auto term = Vocabulary::registerTerm(kind, QStringLiteral("Status"), status);
    return term ? kSections.value(kind + QLatin1Char('/') + *term) : QString();
}

} // namespace

std::expected<QString, QString> EntryWriter::render(const Entry& entry, const QDate& today)
{
    static const QRegularExpression kName(QStringLiteral(R"(^[A-Za-z][A-Za-z -]*$)"));
    const Layout& layout = layoutOf(entry.kind);
    if (layout.order.isEmpty())
        return std::unexpected(QStringLiteral("`kind` is one of F, C, D, AV, BUG, IMP"));
    if (RegisterParser::canonical(entry.ident).isEmpty() || !entry.ident.startsWith(entry.kind + QLatin1Char('-')))
        return std::unexpected(QStringLiteral("%1 is not a %2 ident").arg(entry.ident, entry.kind));
    const QString title = entry.title.trimmed();
    if (title.isEmpty() || title.contains(QLatin1Char('\n')))
        return std::unexpected(QStringLiteral("`title` is one line of text"));

    // The caller's fields, by name (case as the standard writes it), then defaults for the rest.
    QList<QPair<QString, QString>> fields;
    const auto find = [&fields](const QString& name) {
        return std::find_if(fields.begin(), fields.end(), [&name](const auto& f) { return f.first.compare(name, Qt::CaseInsensitive) == 0; });
    };
    for (const auto& [rawName, rawValue] : entry.fields) {
        const QString name = rawName.trimmed();
        if (!kName.match(name).hasMatch())
            return std::unexpected(QStringLiteral("\"%1\" is not a field name: letters, spaces and hyphens").arg(rawName));
        if (find(name) != fields.end())
            return std::unexpected(QStringLiteral("The field %1 is given twice").arg(name));
        QString canonicalName = name;
        for (const QString& known : layout.order) {
            if (known.compare(name, Qt::CaseInsensitive) == 0)
                canonicalName = known;
        }
        QString value = rawValue;
        value.replace(QLatin1String("\r\n"), QLatin1String("\n"));
        fields.append({canonicalName, value.trimmed()});
    }
    for (const auto& [name, value] : defaults(entry.kind, today)) {
        if (find(name) == fields.end())
            fields.append({name, value});
    }

    // What the lint would call an error, refused now (AV-012).
    for (const QString& name : required(entry.kind)) {
        const auto it = find(name);
        if (it == fields.end() || it->second.isEmpty())
            return std::unexpected(QStringLiteral("A %1 entry needs a %2 field (the lint's L5–L7)").arg(entry.kind, name));
    }
    for (const auto& [name, value] : std::as_const(fields)) {
        const auto allowed = Vocabulary::forRegisterField(entry.kind, name);
        if (!allowed)
            continue;
        if (value.contains(QLatin1Char('\n')) || !Vocabulary::registerTerm(entry.kind, name, value))
            return std::unexpected(QStringLiteral("%1 \"%2\" is not one of: %3 (a qualifier may follow, as in \"fixed (2026-10-14)\")")
                                       .arg(name, value, allowed->join(QStringLiteral(", "))));
    }

    // The kind's usual order; fields it does not know follow, by name.
    std::stable_sort(fields.begin(), fields.end(), [&layout](const auto& a, const auto& b) {
        const qsizetype x = layout.order.indexOf(a.first);
        const qsizetype y = layout.order.indexOf(b.first);
        if ((x < 0) != (y < 0))
            return x >= 0;
        return x >= 0 ? x < y : a.first.compare(b.first, Qt::CaseInsensitive) < 0;
    });

    const bool colon = entry.kind == QLatin1String("BUG") || entry.kind == QLatin1String("IMP");
    QString text = QStringLiteral("### %1%2 %3\n").arg(entry.ident, colon ? QStringLiteral(":") : QString(), title);
    for (const auto& [name, value] : std::as_const(fields)) {
        const bool single = layout.single.contains(name);
        const bool list = value.startsWith(QLatin1String("- ")) || value.startsWith(QLatin1String("1. "));
        if (!single && layout.spaced)
            text += QLatin1Char('\n'); // D and C paragraphs stand apart
        if (single)
            text += value.contains(QLatin1Char('\n')) || list ? QStringLiteral("**%1:**\n%2\n").arg(name, value)
                                                              : QStringLiteral("**%1:** %2\n").arg(name, value);
        else
            text += list ? QStringLiteral("**%1.**\n%2\n").arg(name, value) : QStringLiteral("**%1.** %2\n").arg(name, value);
    }
    return text;
}

EntryWriter::Placed EntryWriter::place(const QString& file, const QString& kind, const QString& status, const QString& entryText)
{
    static const QRegularExpression kEntry(QStringLiteral(R"(^### (?:F|C|D|AV|BUG|IMP)-(\d{3,}))"));
    QStringList lines = file.split(QLatin1Char('\n'));
    if (!lines.isEmpty() && lines.back().isEmpty())
        lines.removeLast(); // the piece after a final newline
    QStringList entryLines = entryText.split(QLatin1Char('\n'));
    if (!entryLines.isEmpty() && entryLines.back().isEmpty())
        entryLines.removeLast();

    qsizetype at = -1; // insert before this line
    const QString section = sectionFor(kind, status);
    if (!section.isEmpty()) {
        qsizetype start = -1;
        qsizetype end = lines.size();
        for (qsizetype i = 0; i < lines.size(); ++i) {
            if (start < 0 && lines[i].trimmed().compare(QStringLiteral("## ") + section, Qt::CaseInsensitive) == 0) {
                start = i;
            } else if (start >= 0 && lines[i].startsWith(QLatin1String("## "))) {
                end = i;
                break;
            }
        }
        if (start >= 0) {
            QList<QPair<qsizetype, int>> entries; // line, number
            for (qsizetype i = start + 1; i < end; ++i) {
                if (const auto m = kEntry.match(lines[i]); m.hasMatch())
                    entries.append({i, m.captured(1).toInt()});
            }
            for (qsizetype i = start + 1; i < end; ++i) {
                if (lines[i].trimmed() == QLatin1String("_None._")) {
                    lines.removeAt(i); // and the blank line after it, so no gap doubles
                    --end;
                    if (i < end && lines[i].trimmed().isEmpty()) {
                        lines.removeAt(i);
                        --end;
                    }
                    break;
                }
            }
            const bool newestFirst = entries.size() >= 2 && entries.front().second > entries.back().second;
            if (newestFirst) {
                at = entries.front().first;
            } else {
                at = end;
                while (at - 1 > start && lines[at - 1].trimmed().isEmpty())
                    --at; // after the section's last text, before its trailing blank lines
            }
        }
    }
    if (at < 0) {
        while (!lines.isEmpty() && lines.back().trimmed().isEmpty())
            lines.removeLast(); // the file ends with the new entry
        at = lines.size();
    }

    // One blank line before the entry and after it.
    QStringList block;
    if (at > 0 && !lines[at - 1].trimmed().isEmpty())
        block.append(QString());
    const int headingLine = int(at + block.size()) + 1;
    block += entryLines;
    if (at < lines.size() && !lines[at].trimmed().isEmpty())
        block.append(QString());
    for (qsizetype i = 0; i < block.size(); ++i)
        lines.insert(at + i, block[i]);
    QString text = lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
    return {text, headingLine};
}

} // namespace coeus
