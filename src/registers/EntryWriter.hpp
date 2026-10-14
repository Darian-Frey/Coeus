#pragma once

#include <QDate>
#include <QList>
#include <QPair>
#include <QString>

#include <expected>

namespace coeus {

// The text of a register entry written for someone else (MCP's
// append_entry, F-033), and where in the file it goes. Pure. An entry the
// lint would reject as an error (a value outside its vocabulary, L3; a
// required field missing, L5–L7) is refused before any file is touched
// (AV-012).
class EntryWriter {
public:
    struct Entry {
        QString kind;  // F C D AV BUG IMP
        QString ident; // e.g. "D-021"
        QString title;
        QList<QPair<QString, QString>> fields; // name -> value, any order
    };

    // The entry as the standard writes it, fields in the kind's usual order
    // (as RegisterTemplates' skeletons) with today's date and the kind's
    // starting status where they are missing, ending in a newline.
    static std::expected<QString, QString> render(const Entry& entry, const QDate& today);

    // `file` with `entryText` added: BUG and IMP entries into the section for
    // their status (`## Open`, `## Suggested`, …), keeping that section's
    // order, newest first or last, and replacing a `_None._`; anything else,
    // or with no such section, at the end. `line` is the entry's heading line.
    struct Placed {
        QString text;
        int line = 0;
    };
    static Placed place(const QString& file, const QString& kind, const QString& status, const QString& entryText);
};

} // namespace coeus
