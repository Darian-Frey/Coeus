#include "search/EmbeddingService.hpp"

#include "index/IndexService.hpp"
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

void EmbeddingService::step()
{
    m_scheduled = false;
    if (m_vaultPath.isEmpty())
        return;
    const auto fail = [this](const QString& why) {
        if (why != m_lastProblem) // say it once, not on every index change
            emit unavailable(why);
        m_lastProblem = why;
    };
    if (!m_embedder) {
        auto loaded = Embedder::load(Embedder::modelPath(m_vaultPath));
        if (!loaded) {
            fail(loaded.error());
            return;
        }
        m_embedder = std::move(*loaded);
        m_lastProblem.clear();
    }
    if (!m_db) {
        if (!QFileInfo::exists(m_indexPath))
            return; // the indexer has not made it yet; its first change wakes us
        auto db = Database::open(m_indexPath);
        if (!db) {
            fail(db.error());
            return;
        }
        m_db.emplace(std::move(*db));
    }
    if (!m_adopted) {
        if (auto adopted = VectorStore::adopt(*m_db, *m_embedder); !adopted) {
            fail(adopted.error());
            return;
        }
        m_adopted = true;
    }
    const auto done = VectorStore::embedPending(*m_db, *m_embedder, kBatch);
    if (!done) {
        fail(done.error());
        return;
    }
    const auto coverage = VectorStore::coverage(*m_db);
    if (coverage)
        emit progress(coverage->embedded, coverage->total);
    if (*done > 0) {
        m_scheduled = true;
        QTimer::singleShot(0, this, &EmbeddingService::step); // the next batch, after anything queued meanwhile
    } else {
        VectorStore::prune(*m_db); // vectors of passages edited away
    }
}

} // namespace coeus
