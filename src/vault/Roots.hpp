#pragma once

#include <QList>
#include <QString>
#include <QStringList>

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
};

} // namespace coeus
