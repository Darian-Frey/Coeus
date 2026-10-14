#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <expected>

namespace coeus {

// A linked project root from `<vault>/.coeus/config.toml` (SPEC.md §Linked
// roots, D-010, F-034). Root ids are assigned by the index, not here.
struct RootConfig {
    QString name;
    QString path; // absolute, ~ expanded, cleaned
};

class Roots {
public:
    // Name of root 0, the vault folder; reserved in config.
    static constexpr const char* kVaultRootName = "vault";

    struct Loaded {
        QList<RootConfig> roots; // valid entries, in file order
        QStringList problems;    // one message per rejected entry or parse error
    };

    static QString configPath(const QString& vaultPath);

    // Reads [[roots]]. A missing file is no roots and no problems; a malformed
    // one is no roots and one problem, so the vault always opens (D-016).
    static Loaded load(const QString& vaultPath);
    // The same, from text; `vaultPath` resolves relative root paths.
    static Loaded parse(const QByteArray& toml, const QString& vaultPath);

    // `toml` with a [[roots]] table for `name` at `path` appended, every other
    // byte kept; fails if the name is invalid or taken, the path already a
    // root, or the text or result not valid config.
    static std::expected<QByteArray, QString> withRoot(const QByteArray& toml, const QString& vaultPath,
                                                       const QString& name, const QString& path);
    // The same edit on the vault's config.toml, written through AtomicWriter
    // (an explicit user command: Promote idea, F-032).
    static std::expected<void, QString> add(const QString& vaultPath, const QString& name, const QString& path);
};

} // namespace coeus
