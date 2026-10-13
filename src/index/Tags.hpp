#pragma once

#include <QList>
#include <QString>

#include <expected>

namespace coeus {

// The tag pane's data (F-007): every tag in the index, nested on '/', with
// the number of notes carrying it or any tag beneath it. Tags compare without
// regard to case, as in Obsidian; a group shows the spelling met first.
class Tags {
public:
    struct Usage {
        QString tag;         // as written, without '#'
        QString notePath;    // absolute
        QString noteDisplay; // "<path>" in the vault, "<root>/<path>" in a linked root
    };

    struct Node {
        QString name;    // this segment, e.g. "index"
        QString fullTag; // the whole tag, e.g. "storage/index"
        int noteCount = 0; // distinct notes with this tag or one beneath it
        QList<Node> children; // sorted by name
    };

    // One row per tag occurrence; read-only, off the UI thread.
    static std::expected<QList<Usage>, QString> load(const QString& indexPath);

    // Pure: the nested tree, top level and children sorted by name.
    static QList<Node> tree(const QList<Usage>& usages);
    // Pure: the distinct notes tagged `tag` or a tag beneath it, sorted by display path.
    static QList<Usage> notesFor(const QList<Usage>& usages, const QString& tag);
};

} // namespace coeus
