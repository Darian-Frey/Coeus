#pragma once

#include <QByteArrayView>
#include <QDir>
#include <QString>
#include <QStringList>

#include <expected>

namespace coeus {

// Filesystem boundary for root 0, the vault folder (ARCHITECTURE.md §vault/).
// Phase 1 only resolves paths and reads notes; writes arrive with AtomicWriter.
class Vault {
public:
    // Fails if the path does not exist or is not a readable directory.
    static std::expected<Vault, QString> open(const QString& path);

    QString rootPath() const;
    QString name() const;

    bool contains(const QString& absolutePath) const;
    QString relativePath(const QString& absolutePath) const;

    // Reads a note as UTF-8. The path must lie inside the vault.
    std::expected<QString, QString> readNote(const QString& absolutePath) const;
    std::expected<QByteArray, QString> readBytes(const QString& absolutePath) const;
    // The editor save path (CLAUDE.md §Invariants): writes through AtomicWriter.
    // The note, or for a new note its folder, must lie inside the vault.
    std::expected<void, QString> writeNote(const QString& absolutePath, QByteArrayView content) const;

    // Name filters for the explorer: Markdown plus attachments (SPEC.md §Vault layout).
    static const QStringList& nameFilters();
    static bool isNote(const QString& path);

private:
    explicit Vault(QDir root);

    QDir m_root;
};

} // namespace coeus
