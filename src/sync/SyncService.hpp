#pragma once

#include "sync/SyncProvider.hpp"

#include <QFutureWatcher>
#include <QObject>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

class QThreadPool;
class QTimer;

namespace coeus {

// Runs the vault's sync (F-028): a pass shortly after the vault opens,
// then every `[sync] interval_s` (300 s), and on Sync Now; one at a time,
// on the sync worker (AV-005). After a pass it reports what changed here
// and the conflict copies waiting. A pass that needs the author's word
// (the first resync, a safety abort, rclone missing) stops the timer
// until it is given. One per vault, held by its session (D-023).
class SyncService : public QObject {
    Q_OBJECT

public:
    // The provider for a vault's settings, or null if sync is not set up.
    using ProviderFactory = std::function<std::unique_ptr<SyncProvider>(const QString& vaultPath)>;

    static constexpr int kDefaultIntervalS = 300;
    static constexpr int kFirstPassDelayMs = 5000; // after opening, once the index has started

    explicit SyncService(QString vaultPath, ProviderFactory factory = {}, QObject* parent = nullptr);
    ~SyncService() override; // stops a running pass and waits for it

    // config.toml's [sync] remote (an rclone remote and path) and rclone (the program).
    static std::unique_ptr<SyncProvider> configuredProvider(const QString& vaultPath);
    static QString configuredRemote(const QString& vaultPath);

    bool isConfigured() const;
    bool isRunning() const { return m_running; }
    // Re-reads the settings (after Set Up Sync…) and schedules a pass.
    void reconfigure();
    // Starts a pass now; false if one is running or sync is not set up.
    bool syncNow(SyncProvider::Mode mode = SyncProvider::Mode::Normal);
    // Looks for conflict copies again, on the sync worker, as after one is resolved.
    void refreshConflicts();

    std::optional<SyncReport> lastReport() const { return m_lastReport; }
    std::optional<SyncError> lastError() const { return m_lastError; }
    QList<SyncConflict> conflicts() const { return m_conflicts; }

signals:
    void started();
    void finished(const coeus::SyncReport& report);
    void failed(const coeus::SyncError& error);
    void conflictsChanged(const QList<coeus::SyncConflict>& conflicts);
    // The settings were read again (reconfigure()).
    void reconfigured();

private:
    struct Outcome {
        std::optional<SyncError> error;
        SyncReport report;
    };
    void schedule(int delayMs);
    void done(const Outcome& outcome);
    void setConflicts(const QList<SyncConflict>& conflicts);

    QString m_vaultPath;
    ProviderFactory m_factory;
    QThreadPool* m_pool = nullptr;
    QTimer* m_timer = nullptr;
    QFutureWatcher<Outcome>* m_watcher = nullptr;
    QFutureWatcher<QList<SyncConflict>>* m_conflictWatcher = nullptr;
    std::atomic_bool m_cancel = false;
    bool m_running = false;
    std::optional<SyncReport> m_lastReport;
    std::optional<SyncError> m_lastError;
    QList<SyncConflict> m_conflicts;
};

} // namespace coeus
