#include "sync/SyncService.hpp"

#include "sync/RcloneProvider.hpp"
#include "sync/SyncConflicts.hpp"
#include "vault/VaultConfig.hpp"

#include <QDir>
#include <QHash>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

namespace coeus {

namespace {

// What sync carries here, by vault-relative path: size and modification time.
QHash<QString, std::pair<qint64, qint64>> snapshot(const QString& vaultPath)
{
    QHash<QString, std::pair<qint64, qint64>> files;
    const QString root = QDir(vaultPath).absolutePath();
    for (const QFileInfo& file : SyncConflicts::syncedFiles(vaultPath, true))
        files.insert(file.absoluteFilePath().mid(root.size() + 1), {file.size(), file.lastModified().toMSecsSinceEpoch()});
    return files;
}

bool pausesTheTimer(SyncError::Kind kind)
{
    // These wait for the author: trying again on a timer would only say so again.
    return kind == SyncError::Kind::NotConfigured || kind == SyncError::Kind::ToolMissing
           || kind == SyncError::Kind::NeedsResync || kind == SyncError::Kind::SafetyAbort;
}

} // namespace

SyncService::SyncService(QString vaultPath, ProviderFactory factory, QObject* parent)
    : QObject(parent)
    , m_vaultPath(std::move(vaultPath))
    , m_factory(factory ? std::move(factory) : ProviderFactory(&SyncService::configuredProvider))
    , m_pool(new QThreadPool(this))
    , m_timer(new QTimer(this))
    , m_watcher(new QFutureWatcher<Outcome>(this))
    , m_conflictWatcher(new QFutureWatcher<QList<SyncConflict>>(this))
{
    m_pool->setMaxThreadCount(1); // the sync worker
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, [this] { syncNow(); });
    connect(m_watcher, &QFutureWatcher<Outcome>::finished, this, [this] { done(m_watcher->result()); });
    connect(m_conflictWatcher, &QFutureWatcher<QList<SyncConflict>>::finished, this,
            [this] { setConflicts(m_conflictWatcher->result()); });
    // Conflict copies left from an earlier session show at once.
    QMetaObject::invokeMethod(this, &SyncService::refreshConflicts, Qt::QueuedConnection);
    if (isConfigured())
        schedule(kFirstPassDelayMs);
}

SyncService::~SyncService()
{
    m_cancel = true;
    m_pool->waitForDone();
}

QString SyncService::configuredRemote(const QString& vaultPath)
{
    return VaultConfig::string(vaultPath, "sync", "remote").value_or(QString()).trimmed();
}

std::unique_ptr<SyncProvider> SyncService::configuredProvider(const QString& vaultPath)
{
    const QString remote = configuredRemote(vaultPath);
    if (remote.isEmpty())
        return nullptr;
    const QString program = VaultConfig::string(vaultPath, "sync", "rclone").value_or(QStringLiteral("rclone"));
    return std::make_unique<RcloneProvider>(remote, program);
}

bool SyncService::isConfigured() const
{
    return m_factory(m_vaultPath) != nullptr;
}

void SyncService::schedule(int delayMs)
{
    m_timer->start(delayMs);
}

void SyncService::reconfigure()
{
    m_lastError.reset();
    if (isConfigured())
        schedule(0);
    else
        m_timer->stop();
    emit reconfigured();
}

bool SyncService::syncNow(SyncProvider::Mode mode)
{
    if (m_running)
        return false;
    std::shared_ptr<SyncProvider> provider = m_factory(m_vaultPath);
    if (!provider)
        return false;
    m_timer->stop();
    m_running = true;
    m_cancel = false;
    emit started();
    m_watcher->setFuture(QtConcurrent::run(m_pool, [vaultPath = m_vaultPath, provider, mode, cancel = &m_cancel]() {
        Outcome outcome;
        const auto before = snapshot(vaultPath);
        if (auto synced = provider->sync(vaultPath, mode, *cancel); !synced)
            outcome.error = synced.error();
        const auto after = snapshot(vaultPath);
        for (auto it = after.cbegin(); it != after.cend(); ++it)
            if (before.value(it.key(), {-1, -1}) != it.value())
                outcome.report.changed.append(it.key());
        for (auto it = before.cbegin(); it != before.cend(); ++it)
            if (!after.contains(it.key()))
                outcome.report.changed.append(it.key());
        outcome.report.changed.sort();
        outcome.report.conflicts = SyncConflicts::find(vaultPath);
        outcome.report.finished = QDateTime::currentDateTime();
        return outcome;
    }));
    return true;
}

void SyncService::done(const Outcome& outcome)
{
    m_running = false;
    setConflicts(outcome.report.conflicts);
    if (outcome.error) {
        m_lastError = outcome.error;
        if (outcome.error->kind != SyncError::Kind::Cancelled) {
            if (!pausesTheTimer(outcome.error->kind))
                schedule(int(VaultConfig::integer(m_vaultPath, "sync", "interval_s").value_or(kDefaultIntervalS)) * 1000);
            emit failed(*outcome.error);
        }
        return;
    }
    m_lastError.reset();
    m_lastReport = outcome.report;
    schedule(int(VaultConfig::integer(m_vaultPath, "sync", "interval_s").value_or(kDefaultIntervalS)) * 1000);
    emit finished(outcome.report);
}

void SyncService::refreshConflicts()
{
    // A walk of the vault: on the sync worker, after any pass in hand (AV-005).
    m_conflictWatcher->setFuture(QtConcurrent::run(m_pool, [vaultPath = m_vaultPath] { return SyncConflicts::find(vaultPath); }));
}

void SyncService::setConflicts(const QList<SyncConflict>& conflicts)
{
    if (conflicts == m_conflicts)
        return;
    m_conflicts = conflicts;
    emit conflictsChanged(m_conflicts);
}

} // namespace coeus
