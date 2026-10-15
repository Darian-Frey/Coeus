#pragma once

#include <QString>
#include <QStringList>

namespace coeus {

// Colours fenced code by language for reading mode (F-025, SPEC.md §Reading
// mode): keywords, types and built-ins, strings, comments and numbers become
// <span class="…"> runs the view's style sheet colours from the theme. A small
// tokeniser per language family, not a parser: it never fails, and a language
// it does not know is escaped and left plain. Pure.
class CodeHighlighter {
public:
    // Span classes, as the view's style sheet names them.
    static constexpr const char* kKeyword = "kw";
    static constexpr const char* kType = "ty";
    static constexpr const char* kString = "st";
    static constexpr const char* kComment = "cm";
    static constexpr const char* kNumber = "nu";

    // `code` as HTML (escaped), coloured if `language` (the fence's info
    // word, any case) is one it knows.
    static QString toHtml(const QString& code, const QString& language);
    static bool knows(const QString& language);
    // Every language name and alias it knows, for SPEC and tests.
    static QStringList languages();
};

} // namespace coeus
