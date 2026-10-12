#pragma once

#include "parser/ParsedNote.hpp"

#include <QByteArrayView>

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
};

} // namespace coeus
