#pragma once

#include "index/IndexQueries.hpp"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

namespace coeus {

// The UI's snapshot of every indexed note and alias, for the [[ completer.
// Reloaded off the UI thread whenever the index changes (AV-005); between
// reloads it answers from the last snapshot.
class NoteCatalogue : public QObject {
    Q_OBJECT

public:
    struct Location {
        QString rootName;
        QString folder; // root-relative, "" at the root
    };

    explicit NoteCatalogue(QObject* parent = nullptr);

    void setIndexPath(const QString& indexPath);
    void reload();

    const QList<NoteEntry>& notes() const { return m_notes; }
    // The root and folder of an absolute path, from the roots in the snapshot.
    Location locate(const QString& absolutePath) const;
    // The note a [[target]] names, as the completer needs it for headings:
    // a title, a root-relative path or root:path. nullptr if unknown.
    const NoteEntry* find(const QString& target, const QString& sourceRoot) const;
    // Headings of the note at `absolutePath` (a short read-only query).
    QStringList headings(const QString& absolutePath) const;

signals:
    void reloaded();

private:
    QString m_indexPath;
    QList<NoteEntry> m_notes;
    int m_generation = 0; // drops results of reloads overtaken by a newer one
};

} // namespace coeus
