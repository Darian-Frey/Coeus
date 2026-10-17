#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace coeus {

// The values the Properties card offers for frontmatter keys (F-008,
// D-025), declared by the author in an ordinary note at the vault's top,
// `Coeus properties.md`, so they sync with the notes and are edited in
// Coeus itself. Each `## key` heading opens a list; its `- value` items
// are the values in order; a `Folders:` line limits the list to notes
// under those folders, named as the Explorer names them (`prose`,
// `pontus/docs`). Anything else in the note is ignored, as is fenced code.
//
// Coeus's own vocabularies apply only where they mean something: the idea
// stages for `stage` in notes under the vault's `ideas/` (§Ideas), unless
// the author declares otherwise. `status` in frontmatter is the author's
// own; a project's status lives in its README's header.
class PropertyVocabulary {
public:
    static constexpr const char* kFileName = "Coeus properties.md";

    struct List {
        QString key;
        QStringList folders; // empty: every note
        QStringList values;
        int line = 0;        // of its heading, 1-based
    };

    static QString filePath(const QString& vaultPath);
    static QList<List> parse(const QString& text);
    // The vault's lists, read again only when the file has changed.
    static QList<List> load(const QString& vaultPath);

    struct Allowed {
        QStringList values;
        bool declared = false; // from the author's note, rather than Coeus's own
    };
    // The values `key` may take in the note at `displayPath` (vault-relative,
    // or `<root>/<path>` in a linked root; `inVault` says which): the declared
    // list whose folder is nearest above the note, else one with no folder,
    // else Coeus's own, else none.
    static std::optional<Allowed> allowed(const QList<List>& lists, const QString& key, const QString& displayPath, bool inVault);

    // What Edit Property Values writes when the note does not exist yet: it explains, and declares nothing.
    static QByteArray starter();
};

} // namespace coeus
