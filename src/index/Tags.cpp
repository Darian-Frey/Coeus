#include "index/Tags.hpp"

#include "index/Database.hpp"

#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <map>

namespace coeus {

namespace {

QString display(const QString& rootName, const QString& path)
{
    return rootName == QLatin1String("vault") ? path : rootName + QLatin1Char('/') + path;
}

// Builder node: children keyed by folded segment, notes as a set.
struct Building {
    QString name;
    QString fullTag;
    QSet<QString> notes;
    std::map<QString, Building> children;
};

Tags::Node finish(const Building& b)
{
    Tags::Node node{b.name, b.fullTag, int(b.notes.size()), {}};
    std::vector<const Building*> ordered;
    for (const auto& [key, child] : b.children)
        ordered.push_back(&child);
    std::sort(ordered.begin(), ordered.end(),
              [](const Building* x, const Building* y) { return x->name.localeAwareCompare(y->name) < 0; });
    for (const Building* child : ordered)
        node.children.append(finish(*child));
    return node;
}

bool isUnder(const QString& tag, const QString& prefix)
{
    const QString folded = tag.toCaseFolded();
    const QString base = prefix.toCaseFolded();
    return folded == base || folded.startsWith(base + QLatin1Char('/'));
}

} // namespace

std::expected<QList<Tags::Usage>, QString> Tags::load(const QString& indexPath)
{
    if (!QFileInfo::exists(indexPath))
        return std::unexpected(QStringLiteral("The index is not built yet"));
    auto db = Database::open(indexPath, Database::Mode::ReadOnly);
    if (!db)
        return std::unexpected(db.error());
    auto query = db->prepare("SELECT t.tag, r.path, n.path, r.name FROM tags t JOIN notes n ON n.id = t.note "
                             "JOIN roots r ON r.id = n.root_id ORDER BY n.root_id, n.path, t.line");
    if (!query)
        return std::unexpected(query.error());
    QList<Usage> usages;
    for (;;) {
        const auto row = query->step();
        if (!row)
            return std::unexpected(row.error());
        if (!*row)
            return usages;
        usages.append({query->text(0), query->text(1) + QLatin1Char('/') + query->text(2),
                       display(query->text(3), query->text(2))});
    }
}

QList<Tags::Node> Tags::tree(const QList<Usage>& usages)
{
    Building root;
    for (const Usage& usage : usages) {
        const QStringList segments = usage.tag.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        Building* node = &root;
        QString full;
        for (const QString& segment : segments) {
            full = full.isEmpty() ? segment : full + QLatin1Char('/') + segment;
            Building& child = node->children[segment.toCaseFolded()];
            if (child.name.isEmpty()) {
                child.name = segment; // first spelling met
                child.fullTag = full;
            }
            child.notes.insert(usage.notePath);
            node = &child;
        }
    }
    return finish(root).children;
}

QList<Tags::Usage> Tags::notesFor(const QList<Usage>& usages, const QString& tag)
{
    QList<Usage> notes;
    QSet<QString> seen;
    for (const Usage& usage : usages) {
        if (isUnder(usage.tag, tag) && !seen.contains(usage.notePath)) {
            seen.insert(usage.notePath);
            notes.append(usage);
        }
    }
    std::sort(notes.begin(), notes.end(),
              [](const Usage& a, const Usage& b) { return a.noteDisplay.localeAwareCompare(b.noteDisplay) < 0; });
    return notes;
}

} // namespace coeus
