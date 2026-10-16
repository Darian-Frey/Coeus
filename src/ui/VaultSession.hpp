#pragma once

#include "ui/ExplorerPanel.hpp"
#include "vault/Bookmarks.hpp"
#include "vault/RecentNotes.hpp"
#include "vault/Vault.hpp"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>

#include <expected>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

class QThread;
class QTimer;

namespace coeus {

class EmbeddingService;
class IndexService;
class MainWindow;
class McpLink;

// One vault's session, shared by every window that has the vault open
// (F-012, D-023): what there can be only one of per vault. The index
// service is the index's only writer, the embedding worker its only
// embedder, the MCP link can listen once, and bookmarks and recent notes
// are one file each. Each window keeps its own panels, catalogue, render
// service, Git worker and journal, reading the same index.
class VaultSession : public QObject {
    Q_OBJECT

public:
    struct LinkedRoot {
        QString name;
        Vault vault;
    };

    // The session of `vault`: the one a window already has open, or a new
    // one, started (`*created` says which).
    static std::shared_ptr<VaultSession> open(const Vault& vault, bool* created = nullptr);
    ~VaultSession() override;

    const Vault& vault() const { return m_vault; }
    // Present linked roots from config.toml (F-034); the explorer's list includes missing ones.
    const std::vector<LinkedRoot>& linkedRoots() const { return m_linkedRoots; }
    const QList<ExplorerRoot>& explorerRoots() const { return m_explorerRoots; }
    // Reads config.toml's roots again and restarts the index on them (Promote
    // idea to a new linked root); windows reconnect on restarted().
    void reloadRoots();

    IndexService* index() const { return m_index; }
    EmbeddingService* embedding() const { return m_embedding; }
    McpLink* mcpLink() const { return m_mcpLink; }
    // For a window attaching after the workers have reported: the note count
    // and problems once the index has opened, and the embedding's last word.
    std::optional<std::pair<int, QStringList>> indexState() const { return m_indexState; }
    std::optional<std::pair<int, int>> embeddingState() const { return m_embeddingState; }
    std::optional<QString> embeddingUnavailable() const { return m_embeddingUnavailable; }

    // The windows sharing the session, in the order they attached.
    void attach(MainWindow* window);
    void detach(MainWindow* window);
    QList<MainWindow*> windows() const;

    // Bookmarks (F-014): saved at once, and shown in every window.
    const QList<Bookmark>& bookmarks() const { return m_bookmarks; }
    std::expected<void, QString> setBookmarks(const QList<Bookmark>& bookmarks);
    // Recent notes (F-014): written a moment after they settle, and on saveRecent().
    const QList<RecentNotes::Note>& recentNotes() const { return m_recent; }
    void touchRecent(const RecentNotes::Note& note);
    std::expected<void, QString> saveRecent();

    static constexpr int kRecentSaveDelayMs = 2000;

signals:
    // The index and embedding workers were replaced (reloadRoots()).
    void restarted();
    void rootsChanged();
    void bookmarksChanged();
    void warning(const QString& message);

private:
    explicit VaultSession(const Vault& vault);
    void loadRoots();
    void start();
    void stop();
    MainWindow* windowFor(const QString& absolutePath) const;

    Vault m_vault;
    std::vector<LinkedRoot> m_linkedRoots;
    QList<ExplorerRoot> m_explorerRoots;
    QList<QPointer<MainWindow>> m_windows;
    QThread* m_indexThread = nullptr;
    IndexService* m_index = nullptr;
    QThread* m_embedThread = nullptr;
    EmbeddingService* m_embedding = nullptr;
    McpLink* m_mcpLink = nullptr;
    std::optional<std::pair<int, QStringList>> m_indexState;
    std::optional<std::pair<int, int>> m_embeddingState;
    std::optional<QString> m_embeddingUnavailable;
    QList<Bookmark> m_bookmarks;
    QList<RecentNotes::Note> m_recent;
    QTimer* m_recentTimer = nullptr;
    bool m_recentPending = false; // touched since last written
};

} // namespace coeus
