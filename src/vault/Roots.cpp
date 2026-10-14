#include "vault/Roots.hpp"

#include "vault/AtomicWriter.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include "vault/Toml.hpp"

#include <string_view>

namespace coeus {

namespace {

QString expandPath(QString path, const QString& vaultPath)
{
    if (path == QLatin1String("~") || path.startsWith(QLatin1String("~/")))
        path.replace(0, 1, QDir::homePath());
    if (QDir::isRelativePath(path))
        path = vaultPath + QLatin1Char('/') + path;
    return QDir::cleanPath(path);
}

} // namespace

QString Roots::configPath(const QString& vaultPath)
{
    return vaultPath + QStringLiteral("/.coeus/config.toml");
}

Roots::Loaded Roots::load(const QString& vaultPath)
{
    QFile file(configPath(vaultPath));
    if (!file.exists())
        return {};
    if (!file.open(QIODevice::ReadOnly))
        return {{}, {QStringLiteral("Cannot read %1: %2").arg(file.fileName(), file.errorString())}};
    return parse(file.readAll(), vaultPath);
}

Roots::Loaded Roots::parse(const QByteArray& text, const QString& vaultPath)
{
    Loaded loaded;
    const toml::parse_result result = toml::parse(std::string_view(text.constData(), size_t(text.size())));
    if (!result) {
        const toml::parse_error& error = result.error();
        loaded.problems.append(QStringLiteral("config.toml line %1, column %2: %3")
                                   .arg(error.source().begin.line)
                                   .arg(error.source().begin.column)
                                   .arg(QString::fromUtf8(error.description().data(), qsizetype(error.description().size()))));
        return loaded;
    }

    const toml::table& table = result.table();
    const toml::node* rootsNode = table.get("roots");
    if (!rootsNode)
        return loaded;
    const toml::array* entries = rootsNode->as_array();
    if (!entries) {
        loaded.problems.append(QStringLiteral("config.toml: roots must be written as [[roots]] tables"));
        return loaded;
    }

    static const QRegularExpression kName(QStringLiteral(R"(^[A-Za-z0-9][A-Za-z0-9_.-]*$)"));
    QSet<QString> names;
    for (size_t i = 0; i < entries->size(); ++i) {
        const toml::table* entry = (*entries)[i].as_table();
        const int line = entry ? int(entry->source().begin.line) : 0;
        auto reject = [&](const QString& why) {
            loaded.problems.append(QStringLiteral("config.toml line %1: root skipped: %2").arg(line).arg(why));
        };
        if (!entry) {
            reject(QStringLiteral("not a table"));
            continue;
        }
        const std::optional<std::string_view> name = (*entry)["name"].value<std::string_view>();
        const std::optional<std::string_view> path = (*entry)["path"].value<std::string_view>();
        if (!name || !path) {
            reject(QStringLiteral("needs both name and path"));
            continue;
        }
        const QString rootName = QString::fromUtf8(name->data(), qsizetype(name->size()));
        if (!kName.match(rootName).hasMatch()) {
            reject(QStringLiteral("name \"%1\" may use only letters, digits, '.', '_' and '-'").arg(rootName));
            continue;
        }
        if (rootName == QLatin1String(kVaultRootName)) {
            reject(QStringLiteral("\"%1\" is reserved for the vault itself").arg(rootName));
            continue;
        }
        if (names.contains(rootName)) {
            reject(QStringLiteral("duplicate name \"%1\"").arg(rootName));
            continue;
        }
        names.insert(rootName);
        loaded.roots.append({rootName, expandPath(QString::fromUtf8(path->data(), qsizetype(path->size())), vaultPath)});
    }
    return loaded;
}

std::expected<QByteArray, QString> Roots::withRoot(const QByteArray& toml, const QString& vaultPath,
                                                    const QString& name, const QString& path)
{
    if (!toml.isEmpty() && !toml::parse(std::string_view(toml.constData(), size_t(toml.size()))))
        return std::unexpected(QStringLiteral("config.toml is not valid TOML; it was left unchanged"));
    const Loaded before = parse(toml, vaultPath);
    const QString clean = QDir::cleanPath(path);
    for (const RootConfig& root : before.roots) {
        if (root.name == name)
            return std::unexpected(QStringLiteral("A linked root is already called %1").arg(name));
        if (root.path == clean)
            return std::unexpected(QStringLiteral("%1 is already the linked root %2").arg(clean, root.name));
    }
    QString escaped = clean;
    escaped.replace(QLatin1Char('\\'), QLatin1String("\\\\")).replace(QLatin1Char('"'), QLatin1String("\\\""));
    QByteArray updated = toml;
    if (!updated.isEmpty() && !updated.endsWith('\n'))
        updated += '\n';
    if (!updated.isEmpty())
        updated += '\n';
    updated += QStringLiteral("[[roots]]\nname = \"%1\"\npath = \"%2\"\n").arg(name, escaped).toUtf8();
    const Loaded after = parse(updated, vaultPath);
    if (after.roots.size() != before.roots.size() + 1 || after.problems.size() != before.problems.size())
        return std::unexpected(after.problems.isEmpty() ? QStringLiteral("The root could not be added")
                                                        : after.problems.back());
    return updated;
}

std::expected<void, QString> Roots::add(const QString& vaultPath, const QString& name, const QString& path)
{
    const QString file = configPath(vaultPath);
    QByteArray current;
    if (QFile in(file); in.exists()) {
        if (!in.open(QIODevice::ReadOnly))
            return std::unexpected(QStringLiteral("Cannot read %1: %2").arg(file, in.errorString()));
        current = in.readAll();
    }
    const auto updated = withRoot(current, vaultPath, name, path);
    if (!updated)
        return std::unexpected(updated.error());
    if (!QDir().mkpath(QFileInfo(file).absolutePath()))
        return std::unexpected(QStringLiteral("Cannot create %1").arg(QFileInfo(file).absolutePath()));
    return AtomicWriter::write(file, *updated);
}

} // namespace coeus
