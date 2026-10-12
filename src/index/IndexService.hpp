#pragma once

#include "index/IndexStore.hpp"
#include "index/Indexer.hpp"

#include <QHash>
#include <QObject>
#include <QStringList>

#include <memory>
#include <optional>

namespace coeus {

class GitIgnore;
class VaultWatcher;

// The indexer worker (ARCHITECTURE.md §Threading). Lives on its own QThread;
// the UI talks to it only through queued signals and slots, so the UI thread
// never waits on the filesystem or SQLite (AV-005). Owns the index database,
// one GitIgnore and one VaultWatcher per present root, and the Indexer.
class IndexService : public QObject {
    Q_OBJECT

public:
    struct RootState {
        RootInfo info;
        bool present = true;
    };

    explicit IndexService(QObject* parent = nullptr);
    ~IndexService() override;

    static QString indexPath(const QString& vaultPath);

public slots:
    // Opens <vault>/.coeus/index.db, reconciles roots with config.toml, starts
    // the watchers and scans every present root.
    void open(const QString& vaultPath);
    void rescanAll();
    void verify();
    // Discards the index file and builds it again from the vault.
    void rebuild();

signals:
    void opened(int noteCount, const QStringList& problems);
    void indexChanged(int noteCount);
    // Notes changed on disk (absolute paths), or a whole root was rescanned:
    // open editors compare themselves with the disk (F-011).
    void filesChanged(const QStringList& absolutePaths);
    void rootRescanned(const QString& rootPath);
    void busy(const QString& what); // short status text while working
    void verified(const QString& summary, bool clean);
    void failed(const QString& message);
    void warning(const QString& message);

private:
    void applyChanges(int rootId, const QStringList& paths);
    void rescanRoot(int rootId);
    std::optional<Indexer::IgnoreFn> ignoreFor(int rootId) const;
    void startWatching(const RootState& root);
    void reportCount();

    QString m_vaultPath;
    std::optional<IndexStore> m_store;
    std::unique_ptr<Indexer> m_indexer;
    QHash<int, RootState> m_roots;
    QHash<int, std::shared_ptr<GitIgnore>> m_ignores;
    QHash<int, VaultWatcher*> m_watchers;
};

} // namespace coeus
