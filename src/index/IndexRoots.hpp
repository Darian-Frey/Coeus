#pragma once

#include "index/IndexStore.hpp"

#include <QList>
#include <QStringList>

#include <expected>

namespace coeus {

// Brings the index's roots in line with config.toml (SPEC.md §Linked roots):
// root 0 is the vault, linked roots keep their ids by name, new ones take
// the next free id, and roots gone from config are removed with every row
// derived from them. Shared by the GUI's IndexService and `coeus --mcp`.
class IndexRoots {
public:
    struct Root {
        RootInfo info;
        bool present = true; // false: missing on this machine, never scanned (AV-010)
    };
    struct Reconciled {
        QList<Root> roots;    // by id, the vault first
        QStringList problems; // config errors and missing roots, for the author
    };

    static std::expected<Reconciled, QString> reconcile(IndexStore& store, const QString& vaultPath);
};

} // namespace coeus
