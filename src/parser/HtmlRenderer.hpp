#pragma once

#include <QByteArrayView>
#include <QString>

#include <functional>

namespace coeus {

// Renders a note to the HTML subset QTextBrowser displays, for reading mode
// (F-003). Runs md4c's parser, as the index does, and writes the HTML itself
// so the extensions of SPEC.md §Markdown dialect render correctly and never
// inside code (D-014, amended 2026-10-13). Pure: the caller resolves images.
//
// Links carry their meaning in the URL scheme, for the view to act on:
//   coeus-wiki:<target>[#heading]   a wikilink, target percent-encoded
//   coeus-file:<path>[#heading]     a local Markdown link, as written
//   coeus-tag:<name>                a tag
//   anything else                   an external link, unchanged
class HtmlRenderer {
public:
    struct Options {
        // Maps an image or embed target to a URL QTextBrowser can load, or
        // an empty string if it cannot be found. `wiki` is true for ![[…]].
        std::function<QString(const QString& target, bool wiki)> resolveImage;
    };

    static QString render(QByteArrayView utf8, const Options& options = {});

    static QString escape(const QString& text);
};

} // namespace coeus
