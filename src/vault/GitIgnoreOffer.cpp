#include "vault/GitIgnoreOffer.hpp"

#include "vault/AtomicWriter.hpp"
#include "vault/GitIgnore.hpp"
#include "vault/VaultConfig.hpp"

#include <QFile>

namespace coeus {

namespace {

constexpr const char* kTable = "vault";
constexpr const char* kKey = "gitignore_offer";
constexpr const char* kDeclined = "declined";

} // namespace

QStringList GitIgnoreOffer::missingPatterns(const QString& vaultPath)
{
    GitIgnore ignore(vaultPath);
    if (!ignore.inRepository())
        return {};
    QStringList missing;
    if (!ignore.isIgnored(QStringLiteral(".coeus"), true))
        missing.append(QLatin1String(kIndexPattern));
    if (!ignore.isIgnored(QStringLiteral("notes/.note.md.coeus-tmp-0f"), false))
        missing.append(QLatin1String(kTempPattern));
    return missing;
}

bool GitIgnoreOffer::declined(const QString& vaultPath)
{
    return VaultConfig::string(vaultPath, kTable, kKey) == QLatin1String(kDeclined);
}

std::expected<void, QString> GitIgnoreOffer::accept(const QString& vaultPath, const QStringList& patterns)
{
    if (patterns.isEmpty())
        return {};
    const QString path = vaultPath + QStringLiteral("/.gitignore");
    QByteArray content;
    if (QFile file(path); file.exists()) {
        if (!file.open(QIODevice::ReadOnly))
            return std::unexpected(QStringLiteral("Cannot read %1: %2").arg(path, file.errorString()));
        content = file.readAll();
    }
    if (!content.isEmpty() && !content.endsWith('\n'))
        content.append('\n');
    if (!content.isEmpty())
        content.append('\n');
    content.append(kComment).append('\n');
    for (const QString& pattern : patterns)
        content.append(pattern.toUtf8()).append('\n');
    return AtomicWriter::write(path, content);
}

std::expected<void, QString> GitIgnoreOffer::decline(const QString& vaultPath)
{
    return VaultConfig::setString(vaultPath, kTable, kKey, QLatin1String(kDeclined));
}

} // namespace coeus
