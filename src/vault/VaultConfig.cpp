#include "vault/VaultConfig.hpp"

#include "vault/AtomicWriter.hpp"
#include "vault/Roots.hpp"
#include "vault/Toml.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

#include <string_view>

namespace coeus {

namespace {

std::optional<QString> lookup(const QByteArray& text, const char* table, const char* key)
{
    const toml::parse_result result = toml::parse(std::string_view(text.constData(), size_t(text.size())));
    if (!result)
        return std::nullopt;
    const std::optional<std::string_view> value = result.table()[table][key].value<std::string_view>();
    if (!value)
        return std::nullopt;
    return QString::fromUtf8(value->data(), qsizetype(value->size()));
}

QString basicString(const QString& value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\\'), QLatin1String("\\\\")).replace(QLatin1Char('"'), QLatin1String("\\\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

} // namespace

std::optional<QString> VaultConfig::string(const QString& vaultPath, const char* table, const char* key)
{
    QFile file(Roots::configPath(vaultPath));
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return lookup(file.readAll(), table, key);
}

std::optional<qint64> VaultConfig::integer(const QString& vaultPath, const char* table, const char* key)
{
    QFile file(Roots::configPath(vaultPath));
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QByteArray text = file.readAll();
    const toml::parse_result result = toml::parse(std::string_view(text.constData(), size_t(text.size())));
    if (!result)
        return std::nullopt;
    const std::optional<std::int64_t> value = result.table()[table][key].value<std::int64_t>();
    return value ? std::optional<qint64>(*value) : std::nullopt;
}

std::optional<bool> VaultConfig::boolean(const QString& vaultPath, const char* table, const char* key)
{
    QFile file(Roots::configPath(vaultPath));
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QByteArray text = file.readAll();
    const toml::parse_result result = toml::parse(std::string_view(text.constData(), size_t(text.size())));
    if (!result)
        return std::nullopt;
    return result.table()[table][key].value<bool>();
}

std::expected<QByteArray, QString> VaultConfig::withString(const QByteArray& toml, const char* table, const char* key,
                                                           const QString& value)
{
    if (!toml.isEmpty() && !toml::parse(std::string_view(toml.constData(), size_t(toml.size()))))
        return std::unexpected(QStringLiteral("config.toml is not valid TOML; it was left unchanged"));

    const QString tableName = QString::fromLatin1(table);
    const QString keyName = QString::fromLatin1(key);
    const QString line = keyName + QStringLiteral(" = ") + basicString(value);
    const QRegularExpression header(QStringLiteral(R"(^\s*\[\s*%1\s*\]\s*(#.*)?$)").arg(QRegularExpression::escape(tableName)));
    const QRegularExpression anyHeader(QStringLiteral(R"(^\s*\[)"));
    const QRegularExpression keyLine(QStringLiteral(R"(^\s*%1\s*=)").arg(QRegularExpression::escape(keyName)));

    QStringList lines = QString::fromUtf8(toml).split(QLatin1Char('\n'));
    qsizetype headerAt = -1;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        if (header.match(lines[i]).hasMatch()) {
            headerAt = i;
            break;
        }
    }

    if (headerAt < 0) {
        while (!lines.isEmpty() && lines.back().trimmed().isEmpty())
            lines.removeLast();
        if (!lines.isEmpty())
            lines.append(QString());
        lines << QLatin1Char('[') + tableName + QLatin1Char(']') << line << QString();
    } else {
        qsizetype insertAt = headerAt + 1;
        bool replaced = false;
        for (qsizetype i = headerAt + 1; i < lines.size() && !anyHeader.match(lines[i]).hasMatch(); ++i) {
            if (keyLine.match(lines[i]).hasMatch()) {
                lines[i] = line;
                replaced = true;
                break;
            }
        }
        if (!replaced)
            lines.insert(insertAt, line);
    }

    const QByteArray result = lines.join(QLatin1Char('\n')).toUtf8();
    // The edit must parse and say what was asked, or nothing is written.
    if (lookup(result, table, key) != value)
        return std::unexpected(QStringLiteral("Could not set %1.%2 in config.toml; it was left unchanged").arg(tableName, keyName));
    return result;
}

std::expected<void, QString> VaultConfig::setString(const QString& vaultPath, const char* table, const char* key,
                                                    const QString& value)
{
    const QString path = Roots::configPath(vaultPath);
    QByteArray current;
    if (QFile file(path); file.exists()) {
        if (!file.open(QIODevice::ReadOnly))
            return std::unexpected(QStringLiteral("Cannot read %1: %2").arg(path, file.errorString()));
        current = file.readAll();
    }
    const auto updated = withString(current, table, key, value);
    if (!updated)
        return std::unexpected(updated.error());
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return std::unexpected(QStringLiteral("Cannot create %1").arg(QFileInfo(path).absolutePath()));
    return AtomicWriter::write(path, *updated);
}

} // namespace coeus
