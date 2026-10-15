#include "vault/Templates.hpp"

#include "parser/Frontmatter.hpp"
#include "vault/VaultConfig.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>

namespace coeus {

QStringList Templates::nextKinds(const QString& text)
{
    static const QRegularExpression kNext(QStringLiteral(R"(\{\{\s*next:(F|C|D|AV|BUG|IMP)\s*\}\})"));
    QStringList kinds;
    for (auto it = kNext.globalMatch(text); it.hasNext();) {
        const QString kind = it.next().captured(1);
        if (!kinds.contains(kind))
            kinds.append(kind);
    }
    return kinds;
}

Templates::Rendered Templates::render(const QString& text, const QDateTime& now, const QString& title,
                                      const QHash<QString, QString>& nextIds)
{
    static const QRegularExpression kVariable(QStringLiteral(R"(\{\{\s*([A-Za-z]+)(?::([^}]*))?\s*\}\})"));
    Rendered result;
    qsizetype at = 0;
    for (auto it = kVariable.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        result.text += text.mid(at, m.capturedStart() - at);
        at = m.capturedEnd();
        const QString name = m.captured(1).toLower();
        const QString format = m.captured(2).trimmed();
        if (name == QLatin1String("date"))
            result.text += now.date().toString(format.isEmpty() ? QStringLiteral("yyyy-MM-dd") : format);
        else if (name == QLatin1String("time"))
            result.text += now.time().toString(format.isEmpty() ? QStringLiteral("HH:mm") : format);
        else if (name == QLatin1String("title") && format.isEmpty())
            result.text += title;
        else if (name == QLatin1String("next") && nextIds.contains(format))
            result.text += nextIds.value(format); // allocated by NextId (F-010, F-015)
        else {
            result.text += m.captured(0); // left for the author, e.g. {{next:F}} outside a project
            result.unsupported.append(m.captured(0));
        }
    }
    result.text += text.mid(at);
    result.text = withoutPlacement(result.text);
    return result;
}

QString Templates::placement(const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    const Frontmatter::Split split = Frontmatter::split(bytes);
    if (!split.present)
        return {};
    ParsedNote parsed;
    Frontmatter::parse(split, parsed);
    for (const Property& property : std::as_const(parsed.properties)) {
        if (property.key == QLatin1String(kFolderKey) && !property.values.isEmpty()) {
            const QString folder = QDir::cleanPath(property.values.front().trimmed());
            return folder == QLatin1String(".") ? QString() : folder;
        }
    }
    return {};
}

QString Templates::withoutPlacement(const QString& text)
{
    const auto removal = Frontmatter::removeKey(text, QLatin1String(kFolderKey));
    if (!removal)
        return text;
    const QString result = Frontmatter::apply(text, *removal);
    const QByteArray bytes = result.toUtf8();
    const Frontmatter::Split split = Frontmatter::split(bytes);
    if (split.present && QByteArray(split.yaml.data(), split.yaml.size()).trimmed().isEmpty())
        return QString::fromUtf8(split.body.data(), split.body.size()); // it held only the folder
    return result;
}

QString Templates::folder(const QString& vaultPath)
{
    return VaultConfig::string(vaultPath, "templates", "folder").value_or(QLatin1String(kDefaultFolder));
}

QStringList Templates::list(const QString& vaultPath)
{
    const QString root = QDir::cleanPath(vaultPath + QLatin1Char('/') + folder(vaultPath));
    if (!root.startsWith(vaultPath) || !QFileInfo(root).isDir())
        return {};
    QStringList templates;
    QDirIterator it(root, {QStringLiteral("*.md")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        templates.append(it.next());
    templates.sort(Qt::CaseInsensitive);
    return templates;
}

QString Templates::insert(const QString& note, int cursor, const QString& rendered, int* newCursor)
{
    if (note.trimmed().isEmpty()) {
        if (newCursor)
            *newCursor = int(rendered.size());
        return rendered;
    }

    // Separate the template's frontmatter from its body.
    const QByteArray bytes = rendered.toUtf8();
    const Frontmatter::Split split = Frontmatter::split(bytes);
    const QString body = split.present ? QString::fromUtf8(split.body) : rendered;

    // Body at the cursor first, then keys the note lacks; each frontmatter
    // edit comes before the cursor, so the cursor moves by the growth.
    QString text = note;
    const int at = std::clamp(cursor, 0, int(note.size()));
    text.insert(at, body);
    int caret = at + int(body.size());

    if (split.present) {
        ParsedNote templateProps;
        Frontmatter::parse(split, templateProps);
        ParsedNote existing;
        Frontmatter::parse(Frontmatter::split(text.toUtf8()), existing);
        for (const Property& property : std::as_const(templateProps.properties)) {
            const bool present = std::any_of(existing.properties.begin(), existing.properties.end(),
                                             [&property](const Property& p) { return p.key == property.key; });
            if (present)
                continue;
            const QString yaml = property.type == PropertyType::List
                                     ? Frontmatter::yamlList(property.values)
                                     : Frontmatter::yamlScalar(property.values.value(0), property.type);
            const Frontmatter::Edit edit = Frontmatter::setValue(text, property.key, yaml);
            text = Frontmatter::apply(text, edit);
            if (edit.start <= caret)
                caret += int(edit.replacement.size()) - edit.length;
        }
    }
    if (newCursor)
        *newCursor = caret;
    return text;
}

} // namespace coeus
