#include "search/Sidecar.hpp"

#include "vault/AtomicWriter.hpp"
#include "vault/VaultConfig.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSet>
#include <QtEndian>

#include <cstring>

Q_LOGGING_CATEGORY(lcSidecar, "coeus.search")

namespace coeus {

namespace {

constexpr char kMagic[8] = {'C', 'O', 'E', 'U', 'S', 'V', 'E', 'C'};
constexpr int kMaxDim = 8192;
constexpr const char* kStampKey = "sidecar_stamp";
constexpr const char* kCompactedKey = "sidecar_compacted";

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

// What the sidecar and its conflict copies are now: names, sizes and times.
// Unchanged means nothing new to import.
QString stampOf(const QString& vaultPath)
{
    QStringList parts;
    QStringList files = Sidecar::conflictCopies(vaultPath);
    files.prepend(Sidecar::path(vaultPath));
    for (const QString& file : std::as_const(files)) {
        const QFileInfo info(file);
        if (info.exists())
            parts.append(QStringLiteral("%1:%2:%3").arg(info.fileName()).arg(info.size()).arg(info.lastModified().toMSecsSinceEpoch()));
    }
    return parts.join(QLatin1Char('|'));
}

std::expected<QSet<QByteArray>, QString> hashes(Database& db, const char* sql)
{
    auto query = db.prepare(sql);
    if (!query)
        return std::unexpected(query.error());
    QSet<QByteArray> result;
    while (true) {
        const auto row = query->step();
        if (!row)
            return std::unexpected(row.error());
        if (!*row)
            break;
        result.insert(query->blob(0));
    }
    return result;
}

QString relative(const QString& vaultPath, const QString& file)
{
    return QDir(vaultPath).relativeFilePath(file);
}

} // namespace

QString Sidecar::path(const QString& vaultPath)
{
    return vaultPath + QStringLiteral("/.coeus-shared/embeddings.bin");
}

QStringList Sidecar::conflictCopies(const QString& vaultPath)
{
    const QDir folder(vaultPath + QStringLiteral("/.coeus-shared"));
    QStringList copies;
    for (const QString& name : folder.entryList(QDir::Files | QDir::Hidden, QDir::Name)) {
        if (name == QLatin1String("embeddings.bin") || name.startsWith(QLatin1Char('.')) || !name.startsWith(QLatin1String("embeddings")))
            continue; // the sidecar itself, or AtomicWriter's temporary file
        if (name.contains(QLatin1String("conflict"), Qt::CaseInsensitive))
            copies.append(folder.filePath(name));
    }
    return copies;
}

std::expected<Sidecar::Contents, QString> Sidecar::read(const QString& file)
{
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly))
        return std::unexpected(QStringLiteral("cannot be read: %1").arg(in.errorString()));
    const QByteArray data = in.readAll();
    const char* p = data.constData();
    qsizetype left = data.size();
    const auto take = [&](qsizetype n) -> const char* {
        if (left < n)
            return nullptr;
        const char* at = p;
        p += n;
        left -= n;
        return at;
    };
    const char* magic = take(8);
    if (!magic || std::memcmp(magic, kMagic, 8) != 0)
        return std::unexpected(QStringLiteral("is not an embeddings sidecar"));
    const char* field = take(4);
    if (!field)
        return std::unexpected(QStringLiteral("is cut short"));
    if (const quint32 version = qFromLittleEndian<quint32>(field); version != kVersion)
        return std::unexpected(QStringLiteral("has format version %1, which this Coeus does not read").arg(version));
    if (!(field = take(4)))
        return std::unexpected(QStringLiteral("is cut short"));
    const quint32 modelLength = qFromLittleEndian<quint32>(field);
    const char* model = take(qsizetype(modelLength));
    const char* dim = model ? take(4) : nullptr;
    const char* count = dim ? take(8) : nullptr;
    if (!count)
        return std::unexpected(QStringLiteral("is cut short"));
    Contents contents;
    contents.model = QString::fromUtf8(model, modelLength);
    contents.dim = int(qFromLittleEndian<quint32>(dim));
    const quint64 entries = qFromLittleEndian<quint64>(count);
    if (contents.dim <= 0 || contents.dim > kMaxDim)
        return std::unexpected(QStringLiteral("names %1 dimensions").arg(contents.dim));
    const qsizetype vectorBytes = qsizetype(contents.dim) * qsizetype(sizeof(float));
    const qsizetype entryBytes = kHashBytes + vectorBytes;
    if (entries > quint64(left / entryBytes) || qsizetype(entries) * entryBytes != left)
        return std::unexpected(QStringLiteral("holds %1 bytes of entries where its header promises %2 entries").arg(left).arg(entries));
    contents.vectors.reserve(qsizetype(entries));
    for (quint64 i = 0; i < entries; ++i) {
        const char* entry = take(entryBytes);
        contents.vectors.insert(QByteArray(entry, kHashBytes), QByteArray(entry + kHashBytes, vectorBytes));
    }
    return contents;
}

QByteArray Sidecar::serialise(const Contents& contents)
{
    const QByteArray model = contents.model.toUtf8();
    const qsizetype vectorBytes = qsizetype(contents.dim) * qsizetype(sizeof(float));
    QByteArray out;
    out.reserve(8 + 4 + 4 + model.size() + 4 + 8 + contents.vectors.size() * (kHashBytes + vectorBytes));
    const auto u32 = [&out](quint32 value) {
        char bytes[4];
        qToLittleEndian(value, bytes);
        out.append(bytes, 4);
    };
    out.append(kMagic, 8);
    u32(kVersion);
    u32(quint32(model.size()));
    out.append(model);
    u32(quint32(contents.dim));
    char count[8];
    qToLittleEndian(quint64(contents.vectors.size()), count);
    out.append(count, 8);
    // In hash order, so the same vectors always make the same file.
    QList<QByteArray> keys = contents.vectors.keys();
    std::sort(keys.begin(), keys.end());
    for (const QByteArray& hash : std::as_const(keys)) {
        out.append(hash);
        out.append(contents.vectors.value(hash));
    }
    return out;
}

std::expected<Sidecar::Imported, QString> Sidecar::import(Database& db, const QString& vaultPath, const QString& model, int dim)
{
    Imported result;
    const QString stamp = stampOf(vaultPath);
    const auto seen = metaValue(db, kStampKey);
    if (!seen)
        return std::unexpected(seen.error());
    if (stamp == *seen)
        return result;
    result.changed = true;

    const auto missing = hashes(db, "SELECT DISTINCT c.hash FROM chunks c LEFT JOIN vectors v ON v.hash = c.hash WHERE v.hash IS NULL");
    if (!missing)
        return std::unexpected(missing.error());
    QStringList files = conflictCopies(vaultPath);
    if (QFile::exists(path(vaultPath)))
        files.prepend(path(vaultPath));
    QHash<QByteArray, QByteArray> found;
    for (const QString& file : std::as_const(files)) {
        const auto contents = read(file);
        QString why;
        if (!contents)
            why = contents.error();
        else if (contents->model != model || contents->dim != dim)
            why = QStringLiteral("holds %1 (%2 dimensions), not %3 (%4)").arg(contents->model).arg(contents->dim).arg(model).arg(dim);
        if (!why.isEmpty()) {
            result.skipped.append(QStringLiteral("%1 %2: not imported").arg(relative(vaultPath, file), why));
            qCInfo(lcSidecar, "%s", qPrintable(result.skipped.back()));
            continue;
        }
        for (auto it = contents->vectors.cbegin(); it != contents->vectors.cend(); ++it)
            if (missing->contains(it.key()) && !found.contains(it.key()))
                found.insert(it.key(), it.value());
    }
    auto transaction = Transaction::begin(db, Transaction::Kind::Immediate);
    if (!transaction)
        return std::unexpected(transaction.error());
    // OR IGNORE: the embedding worker may have filled one meanwhile.
    auto insert = db.prepare("INSERT OR IGNORE INTO vectors(hash, vec) VALUES (?, ?)");
    if (!insert)
        return std::unexpected(insert.error());
    for (auto it = found.cbegin(); it != found.cend(); ++it) {
        insert->reset();
        if (auto done = insert->bind(1, it.key()).bind(2, it.value()).run(); !done)
            return std::unexpected(done.error());
        result.vectors += int(db.changes());
    }
    if (auto set = setMeta(db, kStampKey, stamp); !set)
        return std::unexpected(set.error());
    if (auto committed = transaction->commit(); !committed)
        return std::unexpected(committed.error());
    if (result.vectors > 0)
        qCInfo(lcSidecar, "Imported %d vector(s) from %s", result.vectors, qPrintable(relative(vaultPath, path(vaultPath))));
    return result;
}

std::expected<Sidecar::Exported, QString> Sidecar::exportTo(Database& db, const QString& vaultPath, const QString& model, int dim)
{
    return write(db, vaultPath, model, dim, false);
}

std::expected<Sidecar::Exported, QString> Sidecar::compact(Database& db, const QString& vaultPath, const QString& model, int dim)
{
    auto result = write(db, vaultPath, model, dim, true);
    if (result) {
        if (auto set = setMeta(db, kCompactedKey, QDateTime::currentDateTimeUtc().toString(Qt::ISODate)); !set)
            return std::unexpected(set.error());
    }
    return result;
}

bool Sidecar::compactionDue(Database& db, const QString& vaultPath)
{
    const qint64 days = VaultConfig::integer(vaultPath, "semantic", "sidecar_compact_days").value_or(kDefaultCompactDays);
    if (days <= 0)
        return false; // turned off
    const auto last = metaValue(db, kCompactedKey);
    if (!last)
        return false;
    const QDateTime when = QDateTime::fromString(*last, Qt::ISODate);
    if (!when.isValid()) {
        (void)setMeta(db, kCompactedKey, QDateTime::currentDateTimeUtc().toString(Qt::ISODate)); // start the clock
        return false;
    }
    return when.daysTo(QDateTime::currentDateTimeUtc()) >= days;
}

std::expected<Sidecar::Exported, QString> Sidecar::write(Database& db, const QString& vaultPath, const QString& model, int dim,
                                                         bool compacting)
{
    // Anything new on disk reaches the index first; the stamp written below
    // then says it has been read.
    if (auto imported = import(db, vaultPath, model, dim); !imported)
        return std::unexpected(imported.error());
    Exported result;
    const qsizetype vectorBytes = qsizetype(dim) * qsizetype(sizeof(float));
    Contents merged{model, dim, {}};
    bool replace = false; // the file on disk is not one to keep as it is

    const QString target = path(vaultPath);
    qsizetype onDisk = 0;
    if (QFile::exists(target)) {
        const auto contents = read(target);
        if (!contents) {
            result.skipped.append(QStringLiteral("%1 %2: replaced").arg(relative(vaultPath, target), contents.error()));
            replace = true;
        } else if (contents->model != model || contents->dim != dim) {
            result.skipped.append(QStringLiteral("%1 holds %2 (%3 dimensions), not %4: replaced")
                                      .arg(relative(vaultPath, target), contents->model)
                                      .arg(contents->dim)
                                      .arg(model));
            replace = true;
        } else {
            merged.vectors = contents->vectors;
            onDisk = merged.vectors.size();
        }
    }
    // Conflict copies: fold in a readable one of this model; remove any
    // readable one once the union is written. One that cannot be read is left.
    QStringList fold;
    for (const QString& copy : conflictCopies(vaultPath)) {
        const auto contents = read(copy);
        if (!contents) {
            result.skipped.append(QStringLiteral("%1 %2: left as it is").arg(relative(vaultPath, copy), contents.error()));
            continue;
        }
        fold.append(copy);
        if (contents->model != model || contents->dim != dim) {
            result.skipped.append(QStringLiteral("%1 holds %2, not %3: removed").arg(relative(vaultPath, copy), contents->model, model));
            continue;
        }
        for (auto it = contents->vectors.cbegin(); it != contents->vectors.cend(); ++it)
            merged.vectors.insert(it.key(), it.value());
    }
    // The index's vectors, which are this model's (VectorStore::adopt).
    {
        auto query = db.prepare("SELECT hash, vec FROM vectors");
        if (!query)
            return std::unexpected(query.error());
        while (true) {
            const auto row = query->step();
            if (!row)
                return std::unexpected(row.error());
            if (!*row)
                break;
            const QByteArray hash = query->blob(0);
            const QByteArray vector = query->blob(1);
            if (hash.size() == kHashBytes && vector.size() == vectorBytes)
                merged.vectors.insert(hash, vector);
        }
    }
    qsizetype dropped = 0;
    if (compacting) {
        const auto present = hashes(db, "SELECT DISTINCT hash FROM chunks");
        if (!present)
            return std::unexpected(present.error());
        for (auto it = merged.vectors.begin(); it != merged.vectors.end();) {
            if (present->contains(it.key())) {
                ++it;
            } else {
                it = merged.vectors.erase(it);
                ++dropped;
            }
        }
    }
    for (const QString& line : std::as_const(result.skipped))
        qCInfo(lcSidecar, "%s", qPrintable(line));
    result.vectors = int(merged.vectors.size());
    const bool grew = merged.vectors.size() + dropped > onDisk;
    if (!replace && fold.isEmpty() && !grew && dropped == 0)
        return result; // the file already holds all of it
    if (merged.vectors.isEmpty() && !QFile::exists(target) && fold.isEmpty())
        return result; // nothing to share yet

    if (!QDir().mkpath(QFileInfo(target).absolutePath()))
        return std::unexpected(QStringLiteral("Cannot create %1").arg(QFileInfo(target).absolutePath()));
    if (auto written = AtomicWriter::write(target, serialise(merged)); !written)
        return std::unexpected(written.error());
    result.written = true;
    for (const QString& copy : std::as_const(fold)) {
        if (QFile::remove(copy))
            ++result.merged;
        else
            qCWarning(lcSidecar, "Could not remove %s", qPrintable(relative(vaultPath, copy)));
    }
    // What is on disk now is what the index holds, or more: nothing to import.
    if (auto set = setMeta(db, kStampKey, stampOf(vaultPath)); !set)
        return std::unexpected(set.error());
    if (compacting)
        qCInfo(lcSidecar, "Compacted %s: %d vector(s) kept, %lld dropped", qPrintable(relative(vaultPath, target)), result.vectors,
               static_cast<long long>(dropped));
    return result;
}

} // namespace coeus
