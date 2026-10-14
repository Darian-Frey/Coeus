#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

#include <expected>
#include <memory>

struct llama_context;
struct llama_model;

namespace coeus {

// The embedding model for semantic search (F-018, D-008): BAAI's
// bge-small-en-v1.5, run on the CPU by llama.cpp inside Coeus. Turns text
// into unit-length vectors whose dot product is cosine similarity. Blocking
// and not thread-safe: one instance per worker thread, never on the UI
// thread (AV-005). The model file comes from tools/fetch_model.py.
class Embedder {
public:
    static constexpr const char* kModelName = "bge-small-en-v1.5";
    static constexpr const char* kFileName = "bge-small-en-v1.5-f16.gguf";
    // bge asks for this before a search query, not before the passages searched.
    static constexpr const char* kQueryPrefix = "Represent this sentence for searching relevant passages: ";

    enum class Purpose { Passage, Query };

    // Where tools/fetch_model.py puts the model: $XDG_DATA_HOME/coeus/models.
    static QString defaultModelPath();
    // The vault's choice ([semantic] model in config.toml), else the default.
    static QString modelPath(const QString& vaultPath);

    static std::expected<std::unique_ptr<Embedder>, QString> load(const QString& path, int threads = 0);
    ~Embedder();
    Embedder(const Embedder&) = delete;
    Embedder& operator=(const Embedder&) = delete;

    QString modelName() const { return QLatin1String(kModelName); }
    int dim() const { return m_dim; }
    // The most tokens a passage can have; longer ones are cut (chunking keeps under it).
    int maxTokens() const { return m_maxTokens; }
    // Tokens `text` makes, special ones included.
    int countTokens(const QString& text) const;

    // One unit vector per text, in order.
    std::expected<QList<QVector<float>>, QString> embed(const QStringList& texts, Purpose purpose = Purpose::Passage);

private:
    Embedder() = default;
    std::vector<int> tokenize(const QString& text) const;

    llama_model* m_model = nullptr;
    llama_context* m_context = nullptr;
    int m_dim = 0;
    int m_maxTokens = 0;
    int m_batchTokens = 0;
};

} // namespace coeus
