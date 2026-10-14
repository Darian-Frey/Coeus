#pragma once

#include "index/Database.hpp"
#include "search/Embedder.hpp"

#include <QObject>
#include <QString>

#include <memory>
#include <optional>

namespace coeus {

// The embedding worker (F-018, ARCHITECTURE.md threading): lives on its own
// low-priority thread and gives every passage in the index a vector, a
// batch at a time, whenever the index changes. Between batches it returns to
// its event loop, so a new request or the vault closing is never kept waiting
// for more than one batch. The UI only hears about it through signals (AV-005).
class EmbeddingService : public QObject {
    Q_OBJECT

public:
    static constexpr int kBatch = 16; // passages per batch, about half a second

    explicit EmbeddingService(QObject* parent = nullptr);
    ~EmbeddingService() override;

public slots:
    // The vault whose index to fill; nothing is embedded until wake(). Call
    // again after the index file is replaced (Rebuild Index).
    void open(const QString& vaultPath);
    // The index changed, or the model was installed: embed what is missing.
    void wake();
    void close();

signals:
    // How far along: `embedded` of `total` distinct passages have vectors.
    void progress(int embedded, int total);
    // Why nothing can be embedded (no model, a model that will not load).
    void unavailable(const QString& why);

private:
    void step();

    QString m_vaultPath;
    QString m_indexPath;
    std::optional<Database> m_db;
    std::unique_ptr<Embedder> m_embedder;
    bool m_scheduled = false;
    bool m_adopted = false;
    QString m_lastProblem;
};

} // namespace coeus
