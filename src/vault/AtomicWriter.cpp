#include "vault/AtomicWriter.hpp"

#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QRandomGenerator>

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

Q_LOGGING_CATEGORY(lcVault, "coeus.vault")

namespace coeus {

namespace {

constexpr int kCreateAttempts = 8;
constexpr mode_t kDefaultMode = 0666; // narrowed by the process umask

QString errnoText(int error)
{
    return QString::fromLocal8Bit(std::strerror(error));
}

// Closes a descriptor on scope exit unless released.
class FdGuard {
public:
    explicit FdGuard(int fd) : m_fd(fd) {}
    ~FdGuard()
    {
        if (m_fd >= 0)
            ::close(m_fd);
    }
    FdGuard(const FdGuard&) = delete;
    FdGuard& operator=(const FdGuard&) = delete;

    int get() const { return m_fd; }
    // Closes now and reports the result; close can surface deferred write errors.
    int close()
    {
        const int result = ::close(m_fd);
        m_fd = -1;
        return result;
    }

private:
    int m_fd;
};

bool writeAll(int fd, QByteArrayView content)
{
    const char* data = content.data();
    qsizetype remaining = content.size();
    while (remaining > 0) {
        const ssize_t written = ::write(fd, data, size_t(remaining));
        if (written < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        data += written;
        remaining -= written;
    }
    return true;
}

std::expected<void, QString> fsyncDirectory(const QByteArray& dir)
{
    const int fd = ::open(dir.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        return std::unexpected(QStringLiteral("Cannot open folder for sync: %1").arg(errnoText(errno)));
    FdGuard guard(fd);
    if (::fsync(fd) != 0)
        return std::unexpected(QStringLiteral("Cannot sync folder: %1").arg(errnoText(errno)));
    return {};
}

} // namespace

std::expected<void, QString> AtomicWriter::write(const QString& path, QByteArrayView content)
{
    // Follow a symlink so the link survives; rename would replace it otherwise.
    const QFileInfo requested(path);
    QString target = requested.absoluteFilePath();
    if (requested.isSymLink()) {
        // canonicalFilePath resolves a chain of links; a dangling link has none.
        target = requested.canonicalFilePath();
        if (target.isEmpty())
            target = requested.symLinkTarget();
    }
    if (target.isEmpty())
        return std::unexpected(QStringLiteral("Cannot resolve %1").arg(path));

    const QFileInfo targetInfo(target);
    const QByteArray targetPath = QFile::encodeName(target);
    const QByteArray dirPath = QFile::encodeName(targetInfo.absolutePath());

    struct stat original {};
    const bool existed = ::stat(targetPath.constData(), &original) == 0;
    if (existed && !S_ISREG(original.st_mode))
        return std::unexpected(QStringLiteral("Not a regular file: %1").arg(path));

    // Create the temporary sibling exclusively, retrying on a name collision.
    QByteArray tempPath;
    int fd = -1;
    for (int attempt = 0; attempt < kCreateAttempts && fd < 0; ++attempt) {
        const QString suffix = QString::number(QRandomGenerator::system()->generate64(), 16);
        tempPath = QFile::encodeName(QStringLiteral("%1/.%2%3%4")
                                         .arg(targetInfo.absolutePath(), targetInfo.fileName(),
                                              QLatin1String(kTempMarker), suffix));
        fd = ::open(tempPath.constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
                    existed ? (original.st_mode & 07777) : kDefaultMode);
        if (fd < 0 && errno != EEXIST)
            return std::unexpected(QStringLiteral("Cannot create temporary file for %1: %2")
                                       .arg(path, errnoText(errno)));
    }
    if (fd < 0)
        return std::unexpected(QStringLiteral("Cannot create temporary file for %1").arg(path));

    FdGuard guard(fd);
    auto fail = [&](const QString& what) -> std::expected<void, QString> {
        const QString message = QStringLiteral("%1 %2: %3").arg(what, path, errnoText(errno));
        ::unlink(tempPath.constData());
        return std::unexpected(message);
    };

    if (existed) {
        // open() applied the umask; set the exact original mode.
        if (::fchmod(fd, original.st_mode & 07777) != 0)
            return fail(QStringLiteral("Cannot set permissions on"));
        if (::fchown(fd, original.st_uid, original.st_gid) != 0) {
            // An ordinary user cannot give a file away; keep the save.
            qCWarning(lcVault, "Could not keep owner of %s: %s", qPrintable(path), std::strerror(errno));
        }
    }

    if (!writeAll(fd, content))
        return fail(QStringLiteral("Cannot write"));
    if (::fsync(fd) != 0)
        return fail(QStringLiteral("Cannot sync"));
    if (guard.close() != 0)
        return fail(QStringLiteral("Cannot close"));
    if (::rename(tempPath.constData(), targetPath.constData()) != 0)
        return fail(QStringLiteral("Cannot replace"));

    // The new content is in place; a failure here only weakens durability.
    if (const auto synced = fsyncDirectory(dirPath); !synced)
        qCWarning(lcVault, "%s after writing %s", qPrintable(synced.error()), qPrintable(path));
    return {};
}

} // namespace coeus
