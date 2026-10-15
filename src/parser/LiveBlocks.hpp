#pragma once

#include <QList>
#include <QString>

namespace coeus {

// The constructs live preview treats as units (F-003, SPEC.md §Live preview):
// shown as written while the cursor is in them, otherwise drawn or folded.
// Lines are 0-based block numbers in the buffer. Pure.
struct LiveBlock {
    enum class Kind {
        Frontmatter, // folded away: the Properties panel shows it
        Code,        // a fence: its ``` lines folded, its code tinted
        Mermaid,     // a ```mermaid fence: drawn as a diagram
        Maths,       // $$ … $$ on lines of its own: drawn
        Picture,     // a line holding only an image or PDF embed: drawn
        Table,       // a GFM table: drawn as reading mode draws it
        Callout,     // > [!kind] and the quote lines under it: drawn
        Embed,       // a line holding only a note embed, ![[Note]]: drawn
    };
    Kind kind = Kind::Code;
    int firstLine = 0;
    int lastLine = 0;
    QString language; // a fence's info word
    QString source;   // Mermaid's or the maths' source; a picture's or embed's target as written;
                      // a table's or callout's lines
    bool wiki = false; // a picture written ![[…]] rather than ![](…)
};

class LiveBlocks {
public:
    static QList<LiveBlock> scan(const QString& text);
};

} // namespace coeus
