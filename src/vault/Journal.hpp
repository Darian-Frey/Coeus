#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

#include <expected>

namespace coeus {

// Unsaved editor buffers, kept so a crash loses at most a few seconds of
// typing (SPEC.md §Autosave journal, F-003, AV-002). One file per note in
// <vault>/.coeus/journal/, named by a hash of the note's absolute path,
// written through AtomicWriter. The journal is Coeus's own data, never a note:
// it lives in .coeus/, which is not synced (D-011) or indexed.
class Journal {
public:
    struct Entry {
        QString notePath;   // absolute
        QByteArray baseHash; // ContentHash of the disk version the text was based on; empty for a new note
        QString text;
        QDateTime written;  // the journal file's mtime
        QString file;       // the journal file
        bool noteChangedSince = false; // the note on disk is newer than this entry
    };

    static QString directory(const QString& vaultPath);
    static QString fileFor(const QString& vaultPath, const QString& notePath);

    static std::expected<void, QString> write(const QString& vaultPath, const QString& notePath,
                                              const QByteArray& baseHash, const QString& text);
    static void remove(const QString& vaultPath, const QString& notePath);

    // Every readable entry; unreadable files are skipped.
    static QList<Entry> entries(const QString& vaultPath);
    // Entries worth offering after a restart: every entry whose text differs
    // from the note on disk (or whose note is gone). An entry older than its
    // note is still offered, flagged noteChangedSince, because it may hold the
    // only copy of unsaved work (AV-002). Entries that match the disk are
    // stale and deleted here.
    static QList<Entry> recoverable(const QString& vaultPath);

    static std::expected<Entry, QString> parse(const QByteArray& content);
    static QByteArray serialise(const QString& notePath, const QByteArray& baseHash, const QString& text);
};

} // namespace coeus
