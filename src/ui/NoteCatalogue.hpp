#pragma once

#include "index/IndexQueries.hpp"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace coeus {

// The UI's snapshot of the index: every note and alias for the [[ completer,
// register entries and projects (F-015), and lint problems (F-016). Reloaded
// off the UI thread whenever the index changes (AV-005); between reloads it
// answers from the last snapshot.
class NoteCatalogue : public QObject {
    Q_OBJECT

public:
    struct Location {
        QString rootName;
        QString folder; // root-relative, "" at the root
    };

    explicit NoteCatalogue(QObject* parent = nullptr);

    void setIndexPath(const QString& indexPath);
    // config.toml [registers] review_threshold_days, for lint L8.
    void setReviewThreshold(int days) { m_reviewThresholdDays = days; }
    int reviewThreshold() const { return m_reviewThresholdDays; }
    void reload();

    const QList<NoteEntry>& notes() const { return m_notes; }
    const QList<RegisterRef>& registers() const { return m_registers; }
    const QList<ProjectRef>& projects() const { return m_projects; }
    // By root, note and line; L8 as of the last reload.
    const QList<ProblemRef>& problems() const { return m_problems; }
    const QList<IdeaRef>& ideas() const { return m_ideas; }
    QList<ProblemRef> problemsFor(const QString& absolutePath) const;
    // The project (root name, root path, folder) of the note at `absolutePath`, if any.
    std::optional<ProjectRef> projectOf(const QString& absolutePath) const;
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
    QList<RegisterRef> m_registers;
    QList<ProjectRef> m_projects;
    QList<ProblemRef> m_problems;
    QList<IdeaRef> m_ideas;
    int m_reviewThresholdDays = 14;
    int m_generation = 0; // drops results of reloads overtaken by a newer one
};

} // namespace coeus
