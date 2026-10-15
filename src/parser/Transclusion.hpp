#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QString>

#include <optional>

namespace coeus {

// What a note embed or link shows of the note it names: the whole body, one
// heading's section (F-023), or one block (F-024). Pure, over the note's text.
class Transclusion {
public:
    // The note's body without its frontmatter.
    static QByteArray body(QByteArrayView note);
    // The section under the first heading whose text is `heading` (case and
    // surrounding space aside), from the heading through the line before the
    // next heading of the same or a higher level, ignoring fenced code;
    // nullopt if there is no such heading.
    static std::optional<QByteArray> section(QByteArrayView note, const QString& heading);

    // A paragraph or list item (F-024): its lines, 1-based in the whole note
    // as the editor and the index number them, and its text without the
    // trailing `^id`. Lines are grouped as Markdown would, roughly: a blank
    // line, a heading, a fence or a list marker ends a block; code is never one.
    struct Block {
        QString id;      // without '^'; empty if it has none
        int firstLine = 0;
        int lastLine = 0;
        QByteArray text;
    };
    // Every block that has an ID, in order.
    static QList<Block> blocks(QByteArrayView note);
    static std::optional<Block> block(QByteArrayView note, const QString& id);
    // The block containing `line`; nullopt on a blank line, a heading or in code.
    static std::optional<Block> blockAt(QByteArrayView note, int line);
    // The 1-based line of the first heading whose text is `heading`, case aside.
    static std::optional<int> headingLine(QByteArrayView note, const QString& heading);
};

} // namespace coeus
