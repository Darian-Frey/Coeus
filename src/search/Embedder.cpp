#include "search/Embedder.hpp"

#include "vault/VaultConfig.hpp"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QThread>

#include <llama.h>

#include <cmath>
#include <mutex>

namespace coeus {

namespace {

// llama.cpp chatters on stderr; Coeus keeps quiet unless something fails,
// and `coeus --mcp` must keep stdout clean in any case.
void quiet(ggml_log_level level, const char* text, void*)
{
    if (level == GGML_LOG_LEVEL_ERROR)
        fputs(text, stderr);
}

void initialiseOnce()
{
    static std::once_flag once;
    std::call_once(once, [] {
        llama_log_set(quiet, nullptr);
        llama_backend_init();
    });
}

} // namespace

QString Embedder::defaultModelPath()
{
    QString data = qEnvironmentVariable("XDG_DATA_HOME");
    if (data.isEmpty())
        data = QDir::homePath() + QStringLiteral("/.local/share");
    return data + QStringLiteral("/coeus/models/") + QLatin1String(kFileName);
}

QString Embedder::modelPath(const QString& vaultPath)
{
    const auto configured = VaultConfig::string(vaultPath, "semantic", "model");
    if (!configured || configured->isEmpty())
        return defaultModelPath();
    QString path = *configured;
    if (path.startsWith(QLatin1String("~/")))
        path = QDir::homePath() + path.mid(1);
    return QDir::isAbsolutePath(path) ? QDir::cleanPath(path) : QDir::cleanPath(vaultPath + QLatin1Char('/') + path);
}

std::expected<std::unique_ptr<Embedder>, QString> Embedder::load(const QString& path, int threads)
{
    if (!QFile::exists(path))
        return std::unexpected(QStringLiteral("No embedding model at %1: run tools/fetch_model.py to download it").arg(path));
    initialiseOnce();
    std::unique_ptr<Embedder> embedder(new Embedder);

    llama_model_params modelParams = llama_model_default_params();
    modelParams.n_gpu_layers = 0; // the CPU, on any machine
    embedder->m_model = llama_model_load_from_file(QFile::encodeName(path).constData(), modelParams);
    if (!embedder->m_model)
        return std::unexpected(QStringLiteral("%1 is not a model llama.cpp can load").arg(path));
    if (llama_model_has_encoder(embedder->m_model) && llama_model_has_decoder(embedder->m_model))
        return std::unexpected(QStringLiteral("%1 is not an embedding model").arg(path));

    embedder->m_dim = llama_model_n_embd(embedder->m_model);
    embedder->m_maxTokens = std::min(512, int(llama_model_n_ctx_train(embedder->m_model)));
    embedder->m_batchTokens = embedder->m_maxTokens * 8; // up to eight full passages per pass

    llama_context_params contextParams = llama_context_default_params();
    contextParams.embeddings = true;
    contextParams.pooling_type = LLAMA_POOLING_TYPE_CLS; // bge pools on the [CLS] token
    contextParams.n_ctx = uint32_t(embedder->m_batchTokens);
    contextParams.n_batch = uint32_t(embedder->m_batchTokens);
    contextParams.n_ubatch = uint32_t(embedder->m_batchTokens); // non-causal: a passage must fit one micro-batch
    contextParams.n_seq_max = 8;
    const int cores = threads > 0 ? threads : std::max(1, QThread::idealThreadCount() / 2);
    contextParams.n_threads = cores;
    contextParams.n_threads_batch = cores;
    embedder->m_context = llama_init_from_model(embedder->m_model, contextParams);
    if (!embedder->m_context)
        return std::unexpected(QStringLiteral("Cannot start the embedding model"));
    return embedder;
}

Embedder::~Embedder()
{
    if (m_context)
        llama_free(m_context);
    if (m_model)
        llama_model_free(m_model);
}

std::vector<int> Embedder::tokenize(const QString& text) const
{
    const QByteArray utf8 = text.toUtf8();
    const llama_vocab* vocab = llama_model_get_vocab(m_model);
    std::vector<llama_token> tokens(size_t(utf8.size()) + 8);
    int count = llama_tokenize(vocab, utf8.constData(), int32_t(utf8.size()), tokens.data(), int32_t(tokens.size()), true, false);
    if (count < 0) {
        tokens.resize(size_t(-count));
        count = llama_tokenize(vocab, utf8.constData(), int32_t(utf8.size()), tokens.data(), int32_t(tokens.size()), true, false);
    }
    tokens.resize(size_t(std::max(0, count)));
    return std::vector<int>(tokens.begin(), tokens.end());
}

int Embedder::countTokens(const QString& text) const
{
    return int(tokenize(text).size());
}

std::expected<QList<QVector<float>>, QString> Embedder::embed(const QStringList& texts, Purpose purpose)
{
    QList<QVector<float>> vectors;
    vectors.reserve(texts.size());
    llama_batch batch = llama_batch_init(m_batchTokens, 0, 8);
    const auto finish = [&](qsizetype from, qsizetype to) -> std::optional<QString> {
        llama_memory_clear(llama_get_memory(m_context), true);
        if (llama_decode(m_context, batch) < 0)
            return QStringLiteral("The embedding model failed");
        for (qsizetype i = from; i < to; ++i) {
            const float* pooled = llama_get_embeddings_seq(m_context, llama_seq_id(i - from));
            if (!pooled)
                return QStringLiteral("The embedding model gave no vector");
            QVector<float> vector(pooled, pooled + m_dim);
            double norm = 0;
            for (const float value : vector)
                norm += double(value) * value;
            const float scale = norm > 0 ? float(1.0 / std::sqrt(norm)) : 0.0f;
            for (float& value : vector)
                value *= scale;
            vectors.append(vector);
        }
        batch.n_tokens = 0;
        return std::nullopt;
    };

    qsizetype first = 0;
    for (qsizetype i = 0; i < texts.size(); ++i) {
        std::vector<int> tokens = tokenize(purpose == Purpose::Query ? QLatin1String(kQueryPrefix) + texts[i] : texts[i]);
        if (int(tokens.size()) > m_maxTokens) {
            const int last = tokens.back(); // keep the closing [SEP]
            tokens.resize(size_t(m_maxTokens));
            tokens.back() = last;
        }
        const int sequence = int(i - first);
        if (sequence == 8 || batch.n_tokens + int(tokens.size()) > m_batchTokens) {
            if (const auto error = finish(first, i)) {
                llama_batch_free(batch);
                return std::unexpected(*error);
            }
            first = i;
        }
        const int seq = int(i - first);
        for (size_t t = 0; t < tokens.size(); ++t) {
            const int at = batch.n_tokens++;
            batch.token[at] = tokens[t];
            batch.pos[at] = llama_pos(t);
            batch.n_seq_id[at] = 1;
            batch.seq_id[at][0] = seq;
            batch.logits[at] = true;
        }
    }
    if (batch.n_tokens > 0) {
        if (const auto error = finish(first, texts.size())) {
            llama_batch_free(batch);
            return std::unexpected(*error);
        }
    }
    llama_batch_free(batch);
    return vectors;
}

} // namespace coeus
