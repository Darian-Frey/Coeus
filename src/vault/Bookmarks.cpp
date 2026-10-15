#include "vault/Bookmarks.hpp"

#include "vault/AtomicWriter.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace coeus {

namespace {

constexpr int kVersion = 1;

} // namespace

QString Bookmarks::path(const QString& vaultPath)
{
    return vaultPath + QStringLiteral("/.coeus/bookmarks.json");
}

QList<Bookmark> Bookmarks::load(const QString& vaultPath)
{
    QFile file(path(vaultPath));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QList<Bookmark> bookmarks;
    const QJsonArray entries = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("bookmarks")).toArray();
    for (const QJsonValue& value : entries) {
        const QJsonObject entry = value.toObject();
        const QString kind = entry.value(QStringLiteral("kind")).toString();
        if (kind == QLatin1String("note")) {
            const QString root = entry.value(QStringLiteral("root")).toString();
            const QString notePath = entry.value(QStringLiteral("path")).toString();
            if (!root.isEmpty() && !notePath.isEmpty())
                bookmarks.append(Bookmark::note(root, notePath));
        } else if (kind == QLatin1String("search")) {
            const QString query = entry.value(QStringLiteral("query")).toString();
            if (!query.trimmed().isEmpty())
                bookmarks.append(Bookmark::search(query, entry.value(QStringLiteral("mode")).toString(QStringLiteral("words"))));
        }
    }
    return bookmarks;
}

std::expected<void, QString> Bookmarks::save(const QString& vaultPath, const QList<Bookmark>& bookmarks)
{
    QJsonArray entries;
    for (const Bookmark& bookmark : bookmarks) {
        if (bookmark.kind == Bookmark::Kind::Note)
            entries.append(QJsonObject{{QStringLiteral("kind"), QStringLiteral("note")},
                                       {QStringLiteral("root"), bookmark.root},
                                       {QStringLiteral("path"), bookmark.path}});
        else
            entries.append(QJsonObject{{QStringLiteral("kind"), QStringLiteral("search")},
                                       {QStringLiteral("query"), bookmark.query},
                                       {QStringLiteral("mode"), bookmark.mode}});
    }
    const QString target = path(vaultPath);
    if (!QDir().mkpath(QFileInfo(target).absolutePath()))
        return std::unexpected(QStringLiteral("Cannot create %1").arg(QFileInfo(target).absolutePath()));
    return AtomicWriter::write(target, QJsonDocument(QJsonObject{{QStringLiteral("version"), kVersion},
                                                                 {QStringLiteral("bookmarks"), entries}})
                                           .toJson(QJsonDocument::Indented));
}

QList<Bookmark> Bookmarks::toggled(QList<Bookmark> bookmarks, const Bookmark& bookmark)
{
    if (!bookmarks.removeOne(bookmark))
        bookmarks.append(bookmark);
    return bookmarks;
}

} // namespace coeus
