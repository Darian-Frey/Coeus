#include "search/EmbeddingService.hpp"

#include "index/IndexService.hpp"
#include "search/Sidecar.hpp"
#include "search/VectorStore.hpp"

#include <QFileInfo>
#include <QTimer>

namespace coeus {

EmbeddingService::EmbeddingService(QObject* parent)
    : QObject(parent)
{
}

EmbeddingService::~EmbeddingService() = default;

void EmbeddingService::open(const QString& vaultPath)
{
    // Reopening the same vault (after Rebuild Index replaced the file) keeps
    // the loaded model but takes a fresh connection.
    std::unique_ptr<Embedder> keep = vaultPath == m_vaultPath ? std::move(m_embedder) : nullptr;
    close();
    m_embedder = std::move(keep);
    m_vaultPath = vaultPath;
    m_indexPath = IndexService::indexPath(vaultPath);
}

void EmbeddingService::close()
{
    m_db.reset();
    m_embedder.reset();
    m_adopted = false;
    m_unshared = 0;
    m_shareChecked = false;
    m_vaultPath.clear();
    m_indexPath.clear();
    m_lastProblem.clear();
}

void EmbeddingService::wake()
{
    if (m_vaultPath.isEmpty() || m_scheduled)
        return;
    m_scheduled = true;
    QTimer::singleShot(0, this, &EmbeddingService::step);
}

bool EmbeddingService::ready()
{
    const auto fail = [this](const QString& why) {
        if (why != m_lastProblem) // say it once, not on every index change
            emit unavailable(why);
        m_lastProblem = why;
        return false;
    };
    if (!m_embedder) {
        auto loaded = Embedder::load(Embedder::modelPath(m_vaultPath));
        if (!loaded)
            return fail(loaded.error());
        m_embedder = std::move(*loaded);
        m_lastProblem.clear();
    }
    if (!m_db) {
        if (!QFileInfo::exists(m_indexPath))
            return false; // the indexer has not made it yet; its first change wakes us
        auto db = Database::open(m_indexPath);
        if (!db)
            return fail(db.error());
        m_db.emplace(std::move(*db));
    }
    if (!m_adopted) {
        if (auto adopted = VectorStore::adopt(*m_db, *m_embedder); !adopted)
            return fail(adopted.error());
        m_adopted = true;
    }
    return true;
}

void EmbeddingService::step()
{
    m_scheduled = false;
    if (m_vaultPath.isEmpty() || !ready())
        return;
    // Vectors another machine shared come before any embedding here (F-035);
    // only read when the sidecar has changed.
    if (const auto imported = Sidecar::import(*m_db, m_vaultPath, m_embedder->modelName(), m_embedder->dim()); !imported) {
        emit sidecarReport(tr("Embeddings sidecar: %1").arg(imported.error()));
    } else {
        if (imported->vectors > 0)
            emit sidecarReport(tr("Imported %n passage vector(s) from the embeddings sidecar", nullptr, imported->vectors));
        for (const QString& line : imported->skipped)
            emit sidecarReport(line);
    }
    const auto done = VectorStore::embedPending(*m_db, *m_embedder, kBatch);
    if (!done) {
        emit unavailable(done.error());
        m_lastProblem = done.error();
        return;
    }
    m_unshared += *done;
    const auto coverage = VectorStore::coverage(*m_db);
    if (coverage)
        emit progress(coverage->embedded, coverage->total);
    if (*done > 0) {
        m_scheduled = true;
        QTimer::singleShot(0, this, &EmbeddingService::step); // the next batch, after anything queued meanwhile
        return;
    }
    VectorStore::prune(*m_db); // vectors of passages edited away
    const bool compacting = Sidecar::compactionDue(*m_db, m_vaultPath);
    // Once per opening as well: the sidecar may lack vectors embedded before
    // it existed, or a sync may have replaced it with an older one.
    if (m_unshared > 0 || compacting || !m_shareChecked || !Sidecar::conflictCopies(m_vaultPath).isEmpty())
        shareVectors(compacting);
}

void EmbeddingService::compactSidecar()
{
    if (m_vaultPath.isEmpty() || !ready()) {
        emit sidecarReport(tr("Cannot compact the embeddings sidecar: %1").arg(m_lastProblem.isEmpty() ? tr("the index is not ready") : m_lastProblem));
        return;
    }
    shareVectors(true);
}

void EmbeddingService::shareVectors(bool compacting)
{
    const auto shared = compacting ? Sidecar::compact(*m_db, m_vaultPath, m_embedder->modelName(), m_embedder->dim())
                                   : Sidecar::exportTo(*m_db, m_vaultPath, m_embedder->modelName(), m_embedder->dim());
    if (!shared) {
        emit sidecarReport(tr("Could not write the embeddings sidecar: %1").arg(shared.error()));
        return;
    }
    m_unshared = 0;
    m_shareChecked = true;
    for (const QString& line : shared->skipped)
        emit sidecarReport(line);
    if (shared->merged > 0)
        emit sidecarReport(tr("Merged %n conflict copy(ies) into the embeddings sidecar", nullptr, shared->merged));
    if (compacting)
        emit sidecarReport(tr("Embeddings sidecar compacted: %n passage vector(s)", nullptr, shared->vectors));
}

} // namespace coeus
