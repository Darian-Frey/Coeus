#include "ui/NoteCatalogue.hpp"

#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

namespace coeus {

NoteCatalogue::NoteCatalogue(QObject* parent)
    : QObject(parent)
{
}

void NoteCatalogue::setIndexPath(const QString& indexPath)
{
    m_indexPath = indexPath;
    m_notes.clear();
    ++m_generation;
}

void NoteCatalogue::reload()
{
    if (m_indexPath.isEmpty())
        return;
    const int generation = ++m_generation;
    auto* watcher = new QFutureWatcher<QList<NoteEntry>>(this);
    connect(watcher, &QFutureWatcher<QList<NoteEntry>>::finished, this, [this, watcher, generation] {
        watcher->deleteLater();
        if (generation != m_generation)
            return;
        m_notes = watcher->result();
        emit reloaded();
    });
    watcher->setFuture(QtConcurrent::run([path = m_indexPath] {
        return IndexQueries::allNotes(path).value_or(QList<NoteEntry>());
    }));
}

NoteCatalogue::Location NoteCatalogue::locate(const QString& absolutePath) const
{
    Location best;
    qsizetype bestLength = -1;
    for (const NoteEntry& note : m_notes) {
        const QString prefix = note.rootPath + QLatin1Char('/');
        if (absolutePath.startsWith(prefix) && prefix.size() > bestLength) {
            bestLength = prefix.size();
            const QString relative = absolutePath.mid(prefix.size());
            const qsizetype slash = relative.lastIndexOf(QLatin1Char('/'));
            best = {note.rootName, slash < 0 ? QString() : relative.left(slash)};
        }
    }
    return best;
}

const NoteEntry* NoteCatalogue::find(const QString& target, const QString& sourceRoot) const
{
    QString root = sourceRoot;
    QString name = target;
    if (const qsizetype colon = target.indexOf(QLatin1Char(':')); colon > 0) {
        root = target.left(colon);
        name = target.mid(colon + 1);
    }
    const QString asPath = name + QStringLiteral(".md");
    const NoteEntry* byTitle = nullptr;
    for (const NoteEntry& note : m_notes) {
        if (note.rootName == root && note.path == asPath)
            return &note;
        if (!byTitle && note.title == name)
            byTitle = &note;
    }
    return byTitle;
}

QStringList NoteCatalogue::headings(const QString& absolutePath) const
{
    return IndexQueries::headings(m_indexPath, absolutePath).value_or(QStringList());
}

} // namespace coeus
