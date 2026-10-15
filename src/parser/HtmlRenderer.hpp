#pragma once

#include <QByteArray>
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
    // A note an embed names, as the caller found it (F-023).
    struct Embedded {
        QString absolutePath;
        QString title;
        QByteArray text;  // the whole note, frontmatter and all
        QString problem;  // why it cannot be shown (not found, ambiguous); empty if it can
    };
    static constexpr int kEmbedDepth = 3; // embeds within embeds, at most

    // A drawn picture of maths, a diagram or a PDF page (D-022), as the
    // caller has it: a URL to show, or an error, or neither while it is drawn.
    struct Picture {
        QString url;
        int width = 0;
        int height = 0;
        QString error;
    };

    struct Options {
        // Maps an image or embed target to a URL QTextBrowser can load, or
        // an empty string if it cannot be found. `wiki` is true for ![[…]].
        std::function<QString(const QString& target, bool wiki)> resolveImage;
        // The note `target` (without #heading) names, written in the note at
        // `fromPath`; unset to show note embeds as links.
        std::function<Embedded(const QString& target, const QString& fromPath)> resolveNote;
        // Pictures for $maths$, ```mermaid blocks and PDF pages; unset, or
        // answering with no URL, leaves them as code, or a card for a PDF.
        std::function<Picture(const QString& tex, bool display)> renderMaths;
        std::function<Picture(const QString& source)> renderMermaid;
        std::function<Picture(const QString& resolvedUrl, int page)> renderPdfPage;
        QString notePath;     // the note being rendered, absolute
        QString embedAccent;  // an embed's left rule; the light theme's accent if empty
    };

    static QString render(QByteArrayView utf8, const Options& options = {});

    static QString escape(const QString& text);
};

} // namespace coeus
