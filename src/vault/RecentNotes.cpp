#include "vault/RecentNotes.hpp"

#include "vault/AtomicWriter.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace coeus {

QString RecentNotes::path(const QString& vaultPath)
{
    return vaultPath + QStringLiteral("/.coeus/recent.json");
}

QList<RecentNotes::Note> RecentNotes::load(const QString& vaultPath)
{
    QFile file(path(vaultPath));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QList<Note> notes;
    for (const QJsonValue& value : QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("recent")).toArray()) {
        const QJsonObject entry = value.toObject();
        const Note note{entry.value(QStringLiteral("root")).toString(), entry.value(QStringLiteral("path")).toString()};
        if (!note.first.isEmpty() && !note.second.isEmpty() && !notes.contains(note) && notes.size() < kLimit)
            notes.append(note);
    }
    return notes;
}

std::expected<void, QString> RecentNotes::save(const QString& vaultPath, const QList<Note>& notes)
{
    QJsonArray entries;
    for (const Note& note : notes)
        entries.append(QJsonObject{{QStringLiteral("root"), note.first}, {QStringLiteral("path"), note.second}});
    const QString target = path(vaultPath);
    if (!QDir().mkpath(QFileInfo(target).absolutePath()))
        return std::unexpected(QStringLiteral("Cannot create %1").arg(QFileInfo(target).absolutePath()));
    return AtomicWriter::write(target, QJsonDocument(QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("recent"), entries}})
                                           .toJson(QJsonDocument::Indented));
}

QList<RecentNotes::Note> RecentNotes::touched(QList<Note> notes, const Note& note)
{
    notes.removeAll(note);
    notes.prepend(note);
    while (notes.size() > kLimit)
        notes.removeLast();
    return notes;
}

} // namespace coeus
