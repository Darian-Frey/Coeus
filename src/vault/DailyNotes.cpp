#include "vault/DailyNotes.hpp"

#include "vault/Templates.hpp"
#include "vault/VaultConfig.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace coeus {

DailyNotes::Settings DailyNotes::settings(const QString& vaultPath)
{
    Settings s;
    if (auto folder = VaultConfig::string(vaultPath, "daily", "folder"))
        s.folder = *folder;
    if (auto format = VaultConfig::string(vaultPath, "daily", "format"); format && !format->trimmed().isEmpty())
        s.format = *format;
    if (auto templatePath = VaultConfig::string(vaultPath, "daily", "template"))
        s.templatePath = *templatePath;
    return s;
}

std::expected<QString, QString> DailyNotes::pathFor(const QString& vaultPath, const QDate& date, const Settings& settings)
{
    const QString name = date.toString(settings.format).trimmed();
    if (name.isEmpty() || name.endsWith(QLatin1Char('/')))
        return std::unexpected(QStringLiteral("The daily note format \"%1\" gives no file name").arg(settings.format));
    const QString path = QDir::cleanPath(vaultPath + QLatin1Char('/') + settings.folder + QLatin1Char('/') + name
                                         + QStringLiteral(".md"));
    if (!path.startsWith(vaultPath + QLatin1Char('/')))
        return std::unexpected(QStringLiteral("The daily note settings point outside the vault: %1").arg(path));
    return path;
}

std::expected<DailyNotes::Opened, QString> DailyNotes::openOrCreate(const Vault& vault, const QDateTime& now)
{
    const Settings s = settings(vault.rootPath());
    const auto path = pathFor(vault.rootPath(), now.date(), s);
    if (!path)
        return std::unexpected(path.error());
    Opened opened{*path, false, {}};
    if (QFileInfo::exists(*path))
        return opened; // never overwritten

    QString content = QStringLiteral("# %1\n").arg(QFileInfo(*path).completeBaseName());
    if (!s.templatePath.isEmpty()) {
        const QString templateFile = QDir::cleanPath(vault.rootPath() + QLatin1Char('/') + s.templatePath);
        QFile file(templateFile);
        if (!vault.contains(templateFile) || !file.open(QIODevice::ReadOnly))
            return std::unexpected(QStringLiteral("Cannot read the daily note template %1").arg(s.templatePath));
        const Templates::Rendered rendered =
            Templates::render(QString::fromUtf8(file.readAll()), now, QFileInfo(*path).completeBaseName());
        content = rendered.text;
        opened.unsupported = rendered.unsupported;
    }

    if (!QDir().mkpath(QFileInfo(*path).absolutePath()))
        return std::unexpected(QStringLiteral("Cannot create %1").arg(QFileInfo(*path).absolutePath()));
    if (auto written = vault.writeNote(*path, content.toUtf8()); !written)
        return std::unexpected(written.error());
    opened.created = true;
    return opened;
}

} // namespace coeus
