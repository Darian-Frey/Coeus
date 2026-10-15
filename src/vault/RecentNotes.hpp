#pragma once

#include <QList>
#include <QPair>
#include <QString>

#include <expected>

namespace coeus {

// The notes opened most recently (F-014), newest first, per machine in
// `<vault>/.coeus/recent.json`. Each is (root name, path within the root).
class RecentNotes {
public:
    using Note = QPair<QString, QString>;
    static constexpr int kLimit = 30;

    static QString path(const QString& vaultPath);
    static QList<Note> load(const QString& vaultPath);
    static std::expected<void, QString> save(const QString& vaultPath, const QList<Note>& notes);
    // `notes` with `note` first, once, and no more than kLimit.
    static QList<Note> touched(QList<Note> notes, const Note& note);
};

} // namespace coeus
