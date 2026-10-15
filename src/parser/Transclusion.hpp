#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

#include <optional>

namespace coeus {

// What a note embed shows of the note it names (F-023): the whole body, or one
// heading's section. Pure, over the embedded note's text.
class Transclusion {
public:
    // The note's body without its frontmatter.
    static QByteArray body(QByteArrayView note);
    // The section under the first heading whose text is `heading` (case and
    // surrounding space aside), from the heading through the line before the
    // next heading of the same or a higher level, ignoring fenced code;
    // nullopt if there is no such heading.
    static std::optional<QByteArray> section(QByteArrayView note, const QString& heading);
};

} // namespace coeus
