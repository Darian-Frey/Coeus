#include "parser/Frontmatter.hpp"

#include <QRegularExpression>

#include <yaml-cpp/yaml.h>

#include <string>

namespace coeus {

namespace {

constexpr QByteArrayView kUtf8Bom("\xEF\xBB\xBF");

// Returns the line at `pos` without its terminator, and advances `pos` past it.
QByteArrayView takeLine(QByteArrayView text, qsizetype& pos)
{
    const qsizetype start = pos;
    qsizetype end = text.indexOf('\n', start);
    if (end < 0)
        end = text.size();
    pos = end < text.size() ? end + 1 : end;
    QByteArrayView line = text.sliced(start, end - start);
    if (line.endsWith('\r'))
        line.chop(1);
    return line;
}

QString toQString(const std::string& s)
{
    return QString::fromStdString(s);
}

PropertyType scalarType(const YAML::Node& node)
{
    // Quoted scalars carry the non-specific tag "!" and are always text.
    if (node.Tag() == "!")
        return PropertyType::Text;
    static const QRegularExpression kNumber(QStringLiteral(R"(^[-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?$)"));
    static const QRegularExpression kDate(QStringLiteral(R"(^\d{4}-\d{2}-\d{2}$)"));
    const QString value = toQString(node.Scalar());
    if (kDate.match(value).hasMatch())
        return PropertyType::Date;
    if (kNumber.match(value).hasMatch())
        return PropertyType::Number;
    return PropertyType::Text;
}

QString nodeText(const YAML::Node& node)
{
    if (node.IsScalar())
        return toQString(node.Scalar());
    if (node.IsNull())
        return {};
    YAML::Emitter out;
    out << YAML::Flow << node;
    return QString::fromUtf8(out.c_str());
}

// tags: and aliases: accept a list, or a comma- or space-separated string.
QStringList listOf(const YAML::Node& node, bool spaceSeparates)
{
    QStringList items;
    if (node.IsSequence()) {
        for (const YAML::Node& item : node)
            items.append(nodeText(item));
    } else if (node.IsScalar()) {
        static const QRegularExpression kCommas(QStringLiteral(R"(\s*,\s*)"));
        static const QRegularExpression kCommasOrSpaces(QStringLiteral(R"([\s,]+)"));
        items = toQString(node.Scalar()).split(spaceSeparates ? kCommasOrSpaces : kCommas, Qt::SkipEmptyParts);
    }
    for (QString& item : items)
        item = item.trimmed();
    items.removeAll(QString());
    return items;
}

// Frontmatter lines of a note, with their character offsets.
struct Lines {
    QStringList lines;  // without terminators
    QList<int> starts;  // offset of each line in the text
    int open = -1;      // index of the opening "---", or -1 when there is no frontmatter
    int close = -1;     // index of the closing delimiter
};

Lines frontmatterLines(const QString& text)
{
    Lines result;
    int offset = 0;
    for (const QString& line : text.split(QLatin1Char('\n'))) {
        result.starts.append(offset);
        result.lines.append(line.endsWith(QLatin1Char('\r')) ? line.chopped(1) : line);
        offset += int(line.size()) + 1;
    }
    const QString first = result.lines.isEmpty() ? QString() : result.lines.front();
    if (first == QLatin1String("---") || first == QStringLiteral("\uFEFF---")) {
        for (qsizetype i = 1; i < result.lines.size(); ++i) {
            if (result.lines[i] == QLatin1String("---") || result.lines[i] == QLatin1String("...")) {
                result.open = 0;
                result.close = int(i);
                break;
            }
        }
    }
    return result;
}

// The key a top-level line defines, or empty.
QString keyOf(const QString& line)
{
    static const QRegularExpression kKey(QStringLiteral(R"(^([^\s#\-][^:]*?|"[^"]*"|'[^']*')\s*:(\s|$))"));
    const auto match = kKey.match(line);
    if (!match.hasMatch())
        return {};
    QString key = match.captured(1).trimmed();
    if ((key.startsWith(QLatin1Char('"')) && key.endsWith(QLatin1Char('"'))) ||
        (key.startsWith(QLatin1Char('\'')) && key.endsWith(QLatin1Char('\''))))
        key = key.mid(1, key.size() - 2);
    return key;
}

bool isContinuation(const QString& line)
{
    return !line.isEmpty() && (line.front().isSpace() || line.startsWith(QLatin1String("- ")) || line == QLatin1String("-"));
}

// [first, last] line indices of `key` and its continuation lines.
std::optional<std::pair<int, int>> keyLines(const Lines& lines, const QString& key)
{
    for (int i = lines.open + 1; i < lines.close; ++i) {
        if (keyOf(lines.lines[i]) != key)
            continue;
        int last = i;
        while (last + 1 < lines.close && isContinuation(lines.lines[last + 1]))
            ++last;
        return std::pair(i, last);
    }
    return std::nullopt;
}

} // namespace

Frontmatter::Edit Frontmatter::setValue(const QString& text, const QString& key, const QString& yamlValue)
{
    const Lines lines = frontmatterLines(text);
    const QString line = yamlScalar(key) + QStringLiteral(": ") + yamlValue;
    if (lines.open < 0)
        return {0, 0, QStringLiteral("---\n") + line + QStringLiteral("\n---\n")};
    if (const auto range = keyLines(lines, key)) {
        // Keep the key exactly as written (quotes, spacing); replace what follows.
        const QString& old = lines.lines[range->first];
        const qsizetype colon = old.indexOf(QLatin1Char(':'), old.startsWith(QLatin1Char('"')) || old.startsWith(QLatin1Char('\''))
                                                                  ? old.indexOf(old.front(), 1) + 1
                                                                  : 0);
        const int start = lines.starts[range->first];
        const int end = lines.starts[range->second] + int(lines.lines[range->second].size());
        return {start, end - start, old.left(colon + 1) + QLatin1Char(' ') + yamlValue};
    }
    // Absent: add it as the last key, just before the closing delimiter.
    return {lines.starts[lines.close], 0, line + QLatin1Char('\n')};
}

std::optional<Frontmatter::Edit> Frontmatter::removeKey(const QString& text, const QString& key)
{
    const Lines lines = frontmatterLines(text);
    if (lines.open < 0)
        return std::nullopt;
    const auto range = keyLines(lines, key);
    if (!range)
        return std::nullopt;
    const int start = lines.starts[range->first];
    const int end = lines.starts[range->second + 1]; // through the line break
    return Edit{start, end - start, QString()};
}

QString Frontmatter::apply(const QString& text, const Edit& edit)
{
    QString result = text;
    result.replace(edit.start, edit.length, edit.replacement);
    return result;
}

QString Frontmatter::yamlScalar(const QString& value, PropertyType type)
{
    // Plain if YAML reads it back unchanged and, for text, as a plain
    // scalar our inference would also call text.
    bool plain = !value.isEmpty() && value == value.trimmed();
    static const QRegularExpression kIndicator(QStringLiteral(R"(^[-?:,\[\]{}#&*!|>'"%@`])"));
    static const QRegularExpression kSpecial(QStringLiteral(R"((: |:$| #|^(true|false|yes|no|on|off|null|~)$))"),
                                             QRegularExpression::CaseInsensitiveOption);
    if (plain && (kIndicator.match(value).hasMatch() || kSpecial.match(value).hasMatch() || value.contains(QLatin1Char('\n'))))
        plain = false;
    if (plain && type == PropertyType::Text) {
        const QByteArray probe = "k: " + value.toUtf8();
        ParsedNote note;
        Frontmatter::Split split;
        split.present = true;
        split.yaml = probe;
        split.yamlFirstLine = 1;
        Frontmatter::parse(split, note);
        plain = !note.frontmatterError && note.properties.size() == 1 && note.properties[0].type == PropertyType::Text
                && note.properties[0].values == QStringList{value};
    }
    if (plain)
        return value;
    QString escaped = value;
    escaped.replace(QLatin1Char('\\'), QLatin1String("\\\\")).replace(QLatin1Char('"'), QLatin1String("\\\""));
    escaped.replace(QLatin1Char('\n'), QLatin1String("\\n"));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

QString Frontmatter::yamlList(const QStringList& values)
{
    QStringList items;
    for (const QString& value : values) {
        QString item = yamlScalar(value);
        // Inside a flow list, commas and brackets also need quoting.
        if (!item.startsWith(QLatin1Char('"')) && (item.contains(QLatin1Char(',')) || item.contains(QLatin1Char('['))
                                                   || item.contains(QLatin1Char(']')) || item.contains(QLatin1Char('{'))
                                                   || item.contains(QLatin1Char('}'))))
            item = QLatin1Char('"') + item + QLatin1Char('"');
        items.append(item);
    }
    return QLatin1Char('[') + items.join(QStringLiteral(", ")) + QLatin1Char(']');
}

Frontmatter::Split Frontmatter::split(QByteArrayView text)
{
    Split result;
    result.body = text;

    qsizetype pos = text.startsWith(kUtf8Bom) ? kUtf8Bom.size() : 0;
    if (takeLine(text, pos) != "---")
        return result;

    const qsizetype yamlStart = pos;
    int line = 2;
    while (pos < text.size()) {
        const qsizetype lineStart = pos;
        const QByteArrayView current = takeLine(text, pos);
        if (current == "---" || current == "...") {
            result.present = true;
            result.yaml = text.sliced(yamlStart, lineStart - yamlStart);
            result.body = text.sliced(pos);
            result.yamlFirstLine = 2;
            result.bodyFirstLine = line + 1;
            return result;
        }
        ++line;
    }
    return result; // unclosed: not frontmatter
}

void Frontmatter::parse(const Split& split, ParsedNote& note)
{
    if (!split.present)
        return;

    YAML::Node root;
    try {
        root = YAML::Load(std::string(split.yaml.data(), size_t(split.yaml.size())));
    } catch (const YAML::Exception& e) {
        // Exceptions stay inside parser/ (ARCHITECTURE.md §Error handling).
        note.frontmatterError = FrontmatterError{
            QString::fromStdString(e.msg), split.yamlFirstLine + std::max(0, e.mark.line)};
        return;
    }

    if (root.IsNull())
        return;
    if (!root.IsMap()) {
        note.frontmatterError = FrontmatterError{QStringLiteral("Frontmatter is not a set of key: value pairs"),
                                                 split.yamlFirstLine};
        return;
    }

    for (const auto& entry : root) {
        const QString key = toQString(entry.first.Scalar());
        const YAML::Node& value = entry.second;
        const int line = split.yamlFirstLine + std::max(0, entry.first.Mark().line);

        Property property;
        property.key = key;
        property.line = line;
        if (value.IsSequence()) {
            property.type = PropertyType::List;
            for (const YAML::Node& item : value)
                property.values.append(nodeText(item));
        } else if (value.IsScalar()) {
            property.type = scalarType(value);
            property.values.append(nodeText(value));
        } else {
            property.values.append(nodeText(value)); // null or nested map: kept as text
        }
        note.properties.append(property);

        if (key == QLatin1String("tags") || key == QLatin1String("tag")) {
            for (QString tag : listOf(value, true)) {
                if (tag.startsWith(QLatin1Char('#')))
                    tag.remove(0, 1);
                if (!tag.isEmpty())
                    note.tags.append(Tag{tag, line});
            }
        } else if (key == QLatin1String("aliases") || key == QLatin1String("alias")) {
            note.aliases.append(listOf(value, false));
        }
    }
}

} // namespace coeus
