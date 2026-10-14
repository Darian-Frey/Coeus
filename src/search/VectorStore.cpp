#include "search/VectorStore.hpp"

#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QtEndian>

#include <algorithm>
#include <limits>
#include <numeric>

namespace coeus {

namespace {

constexpr int kSnippetChars = 240;

std::expected<QString, QString> metaValue(Database& db, const char* key)
{
    auto query = db.prepare("SELECT value FROM meta WHERE key = ?");
    if (!query)
        return std::unexpected(query.error());
    query->bind(1, QString::fromLatin1(key));
    const auto row = query->step();
    if (!row)
        return std::unexpected(row.error());
    return *row ? query->text(0) : QString();
}

std::expected<void, QString> setMeta(Database& db, const char* key, const QString& value)
{
    auto query = db.prepare("INSERT OR REPLACE INTO meta(key, value) VALUES (?, ?)");
    if (!query)
        return std::unexpected(query.error());
    return query->bind(1, QString::fromLatin1(key)).bind(2, value).run();
}

QString snippetOf(const QString& passage)
{
    // The passage starts with its "title › heading" line; the snippet is the prose.
    QString body = passage.section(QLatin1Char('\n'), 1).simplified();
    return body.size() > kSnippetChars ? body.left(kSnippetChars - 1) + QStringLiteral("…") : body;
}

} // namespace

QByteArray VectorStore::pack(const QVector<float>& vector)
{
    QByteArray blob(vector.size() * qsizetype(sizeof(float)), Qt::Uninitialized);
    qToLittleEndian<float>(vector.constData(), vector.size(), blob.data());
    return blob;
}

QVector<float> VectorStore::unpack(const QByteArray& blob)
{
    QVector<float> vector(blob.size() / qsizetype(sizeof(float)));
    qFromLittleEndian<float>(blob.constData(), vector.size(), vector.data());
    return vector;
}

std::expected<void, QString> VectorStore::adopt(Database& db, const Embedder& embedder)
{
    const auto model = metaValue(db, "embed_model");
    const auto dim = metaValue(db, "embed_dim");
    if (!model || !dim)
        return std::unexpected(!model ? model.error() : dim.error());
    if (*model == embedder.modelName() && *dim == QString::number(embedder.dim()))
        return {};
    auto transaction = Transaction::begin(db, Transaction::Kind::Immediate);
    if (!transaction)
        return std::unexpected(transaction.error());
    if (auto cleared = db.exec("DELETE FROM vectors"); !cleared)
        return std::unexpected(cleared.error());
    if (auto set = setMeta(db, "embed_model", embedder.modelName()); !set)
        return std::unexpected(set.error());
    if (auto set = setMeta(db, "embed_dim", QString::number(embedder.dim())); !set)
        return std::unexpected(set.error());
    return transaction->commit();
}

std::expected<VectorStore::Coverage, QString> VectorStore::coverage(Database& db)
{
    auto query = db.prepare("SELECT (SELECT count(*) FROM (SELECT DISTINCT hash FROM chunks)), "
                            "(SELECT count(*) FROM (SELECT DISTINCT c.hash FROM chunks c JOIN vectors v ON v.hash = c.hash))");
    if (!query)
        return std::unexpected(query.error());
    const auto row = query->step();
    if (!row || !*row)
        return std::unexpected(row ? QStringLiteral("No coverage") : row.error());
    return Coverage{int(query->int64(1)), int(query->int64(0))};
}

std::expected<int, QString> VectorStore::embedPending(Database& db, Embedder& embedder, int limit)
{
    QList<QByteArray> hashes;
    QStringList texts;
    {
        auto pending = db.prepare("SELECT c.hash, min(c.text) FROM chunks c LEFT JOIN vectors v ON v.hash = c.hash "
                                  "WHERE v.hash IS NULL GROUP BY c.hash LIMIT ?");
        if (!pending)
            return std::unexpected(pending.error());
        pending->bind(1, limit);
        while (pending->step().value_or(false)) {
            hashes.append(pending->blob(0));
            texts.append(pending->text(1));
        }
    }
    if (hashes.isEmpty())
        return 0;
    // The model runs outside any transaction: the index stays writable meanwhile.
    const auto vectors = embedder.embed(texts);
    if (!vectors)
        return std::unexpected(vectors.error());
    auto transaction = Transaction::begin(db, Transaction::Kind::Immediate);
    if (!transaction)
        return std::unexpected(transaction.error());
    auto insert = db.prepare("INSERT OR REPLACE INTO vectors(hash, vec) VALUES (?, ?)");
    if (!insert)
        return std::unexpected(insert.error());
    for (qsizetype i = 0; i < hashes.size(); ++i) {
        insert->reset();
        if (auto done = insert->bind(1, hashes[i]).bind(2, pack(vectors->at(i))).run(); !done)
            return std::unexpected(done.error());
    }
    if (auto committed = transaction->commit(); !committed)
        return std::unexpected(committed.error());
    return int(hashes.size());
}

std::expected<int, QString> VectorStore::catchUp(Database& db, Embedder& embedder, const QDeadlineTimer& deadline)
{
    int done = 0;
    while (!deadline.hasExpired()) {
        const auto batch = embedPending(db, embedder, 16);
        if (!batch)
            return std::unexpected(batch.error());
        if (*batch == 0)
            break;
        done += *batch;
    }
    return done;
}

std::expected<int, QString> VectorStore::prune(Database& db)
{
    auto remove = db.prepare("DELETE FROM vectors WHERE hash NOT IN (SELECT hash FROM chunks)");
    if (!remove)
        return std::unexpected(remove.error());
    if (auto done = remove->run(); !done)
        return std::unexpected(done.error());
    return int(db.changes());
}

std::expected<QList<SemanticSearch::Hit>, QString> SemanticSearch::nearest(const QString& indexPath, const QVector<float>& query,
                                                                            int limit)
{
    if (!QFileInfo::exists(indexPath))
        return std::unexpected(QStringLiteral("The index is not built yet"));
    auto db = Database::open(indexPath, Database::Mode::ReadOnly);
    if (!db)
        return std::unexpected(db.error());
    auto rows = db->prepare("SELECT r.path || '/' || n.path, n.title, c.heading, c.start_line, c.end_line, c.text, v.vec, r.name, n.path "
                            "FROM chunks c JOIN vectors v ON v.hash = c.hash JOIN notes n ON n.id = c.note "
                            "JOIN roots r ON r.id = n.root_id");
    if (!rows)
        return std::unexpected(rows.error());
    // Every passage scored; each note keeps its best.
    QHash<QString, Hit> best;
    while (rows->step().value_or(false)) {
        const QByteArray blob = rows->blob(6);
        if (blob.size() != query.size() * qsizetype(sizeof(float)))
            continue; // another model's vector: never compared (AV-007)
        const QVector<float> vector = VectorStore::unpack(blob);
        float score = 0;
        for (qsizetype i = 0; i < query.size(); ++i)
            score += query[i] * vector[i];
        const QString path = rows->text(0);
        auto it = best.find(path);
        if (it == best.end() || score > it->score)
            best.insert(path, Hit{path, rows->text(1), rows->text(2),
                                  rows->text(7) == QLatin1String("vault") ? rows->text(8) : rows->text(7) + QLatin1Char('/') + rows->text(8),
                                  int(rows->int64(3)), int(rows->int64(4)), snippetOf(rows->text(5)), score});
    }
    QList<Hit> hits = best.values();
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        return a.score != b.score ? a.score > b.score : a.notePath < b.notePath;
    });
    if (hits.size() > limit)
        hits.resize(limit);
    return hits;
}

std::expected<SemanticSearch::Related, QString> SemanticSearch::related(const QString& indexPath, const QString& notePath, int limit)
{
    if (!QFileInfo::exists(indexPath))
        return std::unexpected(QStringLiteral("The index is not built yet"));
    auto db = Database::open(indexPath, Database::Mode::ReadOnly);
    if (!db)
        return std::unexpected(db.error());

    // The note, by its absolute path.
    std::int64_t noteId = 0;
    {
        auto find = db->prepare("SELECT n.id FROM notes n JOIN roots r ON r.id = n.root_id WHERE r.path || '/' || n.path = ?");
        if (!find)
            return std::unexpected(find.error());
        find->bind(1, notePath);
        if (!find->step().value_or(false))
            return std::unexpected(QStringLiteral("The note is not indexed yet"));
        noteId = find->int64(0);
    }
    Related result;
    QList<QVector<float>> own;
    {
        auto passages = db->prepare("SELECT v.vec FROM chunks c LEFT JOIN vectors v ON v.hash = c.hash WHERE c.note = ?");
        if (!passages)
            return std::unexpected(passages.error());
        passages->bind(1, noteId);
        while (passages->step().value_or(false)) {
            ++result.ownPassages;
            if (!passages->isNull(0)) {
                own.append(VectorStore::unpack(passages->blob(0)));
                ++result.ownEmbedded;
            }
        }
    }
    if (own.isEmpty())
        return result;
    QSet<std::int64_t> skip{noteId}; // itself, and what it already links to
    {
        auto linked = db->prepare("SELECT DISTINCT target_note FROM links WHERE src = ? AND target_note IS NOT NULL");
        if (!linked)
            return std::unexpected(linked.error());
        linked->bind(1, noteId);
        while (linked->step().value_or(false))
            skip.insert(linked->int64(0));
    }
    auto rows = db->prepare("SELECT n.id, r.path || '/' || n.path, n.title, c.heading, c.start_line, c.end_line, c.text, v.vec, r.name, n.path "
                            "FROM chunks c JOIN vectors v ON v.hash = c.hash JOIN notes n ON n.id = c.note "
                            "JOIN roots r ON r.id = n.root_id");
    if (!rows)
        return std::unexpected(rows.error());
    // Every other note's passages, with their similarity to each of this
    // note's. Notes it links to stay in until scoring: they still count
    // towards how typical a passage of this note is.
    struct Candidate {
        std::int64_t note;
        Hit hit;
        QVector<float> similarity; // to own[i]
    };
    QList<Candidate> candidates;
    const qsizetype dim = own.front().size();
    while (rows->step().value_or(false)) {
        const std::int64_t id = rows->int64(0);
        if (id == noteId)
            continue;
        const QByteArray blob = rows->blob(7);
        if (blob.size() != dim * qsizetype(sizeof(float)))
            continue; // another model's vector (AV-007)
        const QVector<float> vector = VectorStore::unpack(blob);
        Candidate candidate{id,
                            Hit{rows->text(1), rows->text(2), rows->text(3),
                                rows->text(8) == QLatin1String("vault") ? rows->text(9) : rows->text(8) + QLatin1Char('/') + rows->text(9),
                                int(rows->int64(4)), int(rows->int64(5)), rows->text(6), 0},
                            QVector<float>(own.size())};
        const float* theirs = vector.constData();
        float* similarity = candidate.similarity.data();
        for (qsizetype i = 0; i < own.size(); ++i) {
            const float* mine = own.at(i).constData();
            float dot = 0;
            for (qsizetype d = 0; d < dim; ++d)
                dot += mine[d] * theirs[d];
            similarity[i] = dot;
        }
        candidates.append(std::move(candidate));
    }
    // How typical each of this note's passages is: its mean similarity to its
    // kHubNeighbours nearest passages elsewhere. Scaffold boilerplate (a
    // register's preamble, a README's status block) is near its copy in every
    // project, so its matches are discounted by that much (one-sided CSLS).
    QVector<float> typical(own.size(), 0);
    for (qsizetype i = 0; i < own.size(); ++i) {
        QVector<float> column;
        column.reserve(candidates.size());
        for (const Candidate& candidate : std::as_const(candidates))
            column.append(candidate.similarity[i]);
        const qsizetype k = std::min<qsizetype>(kHubNeighbours, column.size());
        if (k == 0)
            continue;
        std::partial_sort(column.begin(), column.begin() + k, column.end(), std::greater<float>());
        typical[i] = std::accumulate(column.begin(), column.begin() + k, 0.0f) / float(k);
    }
    QHash<std::int64_t, Hit> best;
    for (Candidate& candidate : candidates) {
        if (skip.contains(candidate.note))
            continue;
        float score = std::numeric_limits<float>::lowest();
        for (qsizetype i = 0; i < own.size(); ++i)
            score = std::max(score, candidate.similarity[i] - typical[i]);
        auto it = best.find(candidate.note);
        if (it == best.end() || score > it->score) {
            candidate.hit.snippet = snippetOf(candidate.hit.snippet);
            candidate.hit.score = score;
            best.insert(candidate.note, candidate.hit);
        }
    }
    result.notes = best.values();
    std::sort(result.notes.begin(), result.notes.end(), [](const Hit& a, const Hit& b) {
        return a.score != b.score ? a.score > b.score : a.notePath < b.notePath;
    });
    if (result.notes.size() > limit)
        result.notes.resize(limit);
    return result;
}

QList<SemanticSearch::Blended> SemanticSearch::blend(const QList<QPair<QString, QPair<QString, QString>>>& wordHits,
                                                     const QList<Hit>& meaningHits, int limit)
{
    constexpr double kFusion = 60.0;
    QHash<QString, Blended> merged;
    QStringList order;
    for (qsizetype rank = 0; rank < wordHits.size(); ++rank) {
        const auto& [path, info] = wordHits[rank];
        Blended& entry = merged[path];
        if (entry.notePath.isEmpty()) {
            entry.notePath = path;
            entry.title = info.first;
            entry.snippet = info.second;
            order.append(path);
        }
        entry.byWords = true;
        entry.score += 1.0 / (kFusion + double(rank + 1));
    }
    for (qsizetype rank = 0; rank < meaningHits.size(); ++rank) {
        const Hit& hit = meaningHits[rank];
        Blended& entry = merged[hit.notePath];
        if (entry.notePath.isEmpty()) {
            entry.notePath = hit.notePath;
            entry.title = hit.title;
            entry.snippet = hit.snippet;
            order.append(hit.notePath);
        }
        entry.byMeaning = true;
        entry.heading = hit.heading;
        entry.line = hit.startLine;
        entry.score += 1.0 / (kFusion + double(rank + 1));
    }
    QList<Blended> results;
    for (const QString& path : std::as_const(order))
        results.append(merged.value(path));
    std::stable_sort(results.begin(), results.end(), [](const Blended& a, const Blended& b) { return a.score > b.score; });
    if (results.size() > limit)
        results.resize(limit);
    return results;
}

} // namespace coeus
