#include "index/IndexRoots.hpp"

#include "index/Database.hpp"
#include "vault/Roots.hpp"

#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <algorithm>

namespace coeus {

std::expected<IndexRoots::Reconciled, QString> IndexRoots::reconcile(IndexStore& store, const QString& vaultPath)
{
    const Roots::Loaded config = Roots::load(vaultPath);
    Reconciled result;
    result.problems = config.problems;
    QHash<QString, int> knownIds;
    int nextId = 1;
    if (const auto known = store.roots()) {
        for (const RootInfo& root : *known) {
            knownIds.insert(root.name, root.id);
            nextId = std::max(nextId, root.id + 1);
        }
    }

    result.roots.append({{0, QLatin1String(Roots::kVaultRootName), vaultPath}, true});
    QSet<QString> configured{QLatin1String(Roots::kVaultRootName)};
    for (const RootConfig& root : config.roots) {
        const int id = knownIds.contains(root.name) ? knownIds.value(root.name) : nextId++;
        const bool present = QFileInfo(root.path).isDir();
        result.roots.append({{id, root.name, root.path}, present});
        configured.insert(root.name);
        if (!present)
            result.problems.append(QCoreApplication::translate("IndexRoots",
                                                               "Linked root \"%1\" is missing on this machine (%2); its notes "
                                                               "are kept but not updated.")
                                       .arg(root.name, root.path));
    }
    std::sort(result.roots.begin(), result.roots.end(), [](const Root& a, const Root& b) { return a.info.id < b.info.id; });

    auto transaction = Transaction::begin(store.database(), Transaction::Kind::Immediate);
    if (!transaction)
        return std::unexpected(transaction.error());
    // Roots removed from config take their rows with them.
    for (auto it = knownIds.constBegin(); it != knownIds.constEnd(); ++it) {
        if (!configured.contains(it.key()) && it.value() != 0) {
            if (auto removed = store.removeRoot(it.value()); !removed)
                return std::unexpected(removed.error());
        }
    }
    for (const Root& root : std::as_const(result.roots)) {
        if (auto put = store.putRoot(root.info, root.present); !put)
            return std::unexpected(put.error());
    }
    if (auto committed = transaction->commit(); !committed)
        return std::unexpected(committed.error());
    return result;
}

} // namespace coeus
