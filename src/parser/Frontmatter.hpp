#pragma once

#include "parser/ParsedNote.hpp"

#include <QByteArrayView>
#include <QString>
#include <QStringList>

#include <optional>

namespace coeus {

// YAML frontmatter at byte 0 (SPEC.md §Markdown dialect, D-015).
class Frontmatter {
public:
    struct Split {
        QByteArrayView yaml; // between the delimiters; empty if absent
        QByteArrayView body; // everything after the closing delimiter
        int yamlFirstLine = 0; // 1-based line of the first YAML line; 0 if absent
        int bodyFirstLine = 1;
        bool present = false;
    };

    // Separates frontmatter from the body. A block is frontmatter only if the
    // file starts with a "---" line and a later "---" or "..." line closes it.
    static Split split(QByteArrayView text);

    // Parses the YAML into `note.properties`, `note.aliases` and frontmatter
    // tags. Invalid YAML sets `note.frontmatterError` and adds nothing.
    static void parse(const Split& split, ParsedNote& note);

    // --- Editing (D-015: only the changed key's lines are rewritten) ---------

    // A replacement of `length` characters at `start` in the note's text.
    struct Edit {
        int start = 0;
        int length = 0;
        QString replacement;
    };

    // Sets `key` to the YAML value `yamlValue` (as yamlScalar/yamlList give).
    // Replaces the key's line and its continuation lines (indented lines or
    // block-list items) with one `key: value` line; adds the key before the
    // closing delimiter if absent; creates frontmatter if there is none.
    // Every other line, comment and order is kept.
    static Edit setValue(const QString& text, const QString& key, const QString& yamlValue);
    // Removes `key` with its continuation lines; nullopt if it is absent.
    static std::optional<Edit> removeKey(const QString& text, const QString& key);
    static QString apply(const QString& text, const Edit& edit);

    // A YAML scalar for `value` that reads back as the same string and, for
    // PropertyType::Text, as text: plain when that is safe, else double-quoted.
    static QString yamlScalar(const QString& value, PropertyType type = PropertyType::Text);
    // A YAML flow list, e.g. [a, "b: c"].
    static QString yamlList(const QStringList& values);
};

} // namespace coeus
