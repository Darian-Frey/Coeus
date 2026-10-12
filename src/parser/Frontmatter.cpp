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

} // namespace

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
