#pragma once

#include "index/Database.hpp"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

#include <expected>

namespace coeus {

// The embeddings sidecar (F-035, D-011, SPEC.md §Embeddings sidecar):
// `<vault>/.coeus-shared/embeddings.bin` carries vectors between machines by
// passage hash, so a second machine embeds only what changed. Only vectors of
// the model in use are imported (AV-007, AV-011). Each call names the model
// by name and dimension, so it can be used without loading it. Blocking:
// worker threads only.
class Sidecar {
public:
    static constexpr quint32 kVersion = 1;
    static constexpr int kHashBytes = 16;
    static constexpr qint64 kDefaultCompactDays = 7;

    struct Contents {
        QString model;
        int dim = 0;
        QHash<QByteArray, QByteArray> vectors; // passage hash → dim little-endian floats
    };

    static QString path(const QString& vaultPath);
    // Copies made by a sync tool's conflict handling, beside the sidecar
    // (`embeddings.bin.conflict1`, `embeddings.conflict-2026-10-14.bin`, …).
    static QStringList conflictCopies(const QString& vaultPath);

    static std::expected<Contents, QString> read(const QString& file);
    static QByteArray serialise(const Contents& contents);

    struct Imported {
        bool changed = false; // the files differed from when they were last read or written
        int vectors = 0;      // added to the index
        QStringList skipped;  // a line per file not imported, and why
    };
    // Adds the vectors of passages the index has and has no vector for, from
    // the sidecar and any conflict copies, if they are `model`'s and have
    // changed since they were last read or written here.
    static std::expected<Imported, QString> import(Database& db, const QString& vaultPath, const QString& model, int dim);

    struct Exported {
        bool written = false;
        int vectors = 0;      // in the file now
        int merged = 0;       // conflict copies folded in and removed
        QStringList skipped;  // a line per file whose vectors were not kept, and why
    };
    // Writes the union of the sidecar, its conflict copies and the index's
    // vectors, atomically, if that adds anything or folds in a conflict copy.
    // A file of another model, or one that cannot be read, is replaced.
    static std::expected<Exported, QString> exportTo(Database& db, const QString& vaultPath, const QString& model, int dim);
    // As exportTo, but keeps only passages the index has; records when.
    static std::expected<Exported, QString> compact(Database& db, const QString& vaultPath, const QString& model, int dim);
    // Whether `[semantic] sidecar_compact_days` have passed since the last
    // compaction. The first time, the clock starts instead.
    static bool compactionDue(Database& db, const QString& vaultPath);

private:
    static std::expected<Exported, QString> write(Database& db, const QString& vaultPath, const QString& model, int dim, bool compacting);
};

} // namespace coeus
