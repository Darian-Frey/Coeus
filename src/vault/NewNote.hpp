#pragma once

#include <QString>

#include <expected>

namespace coeus {

// Where File › New Note… puts a note (F-036, SPEC.md §New notes). Pure: it
// checks paths and reads nothing but whether the file exists.
class NewNote {
public:
    // The note `name` in `folder` (relative to `rootPath`, "" for the root
    // itself), absolute, with ".md" added unless it ends so. Refused, with
    // the reason: an empty name, a name with a slash or starting with a dot,
    // a folder outside the root or hidden, and a note that already exists.
    static std::expected<QString, QString> pathFor(const QString& rootPath, const QString& folder, const QString& name);
    // A note without a template: its name as a heading.
    static QString blank(const QString& absolutePath);
};

} // namespace coeus
