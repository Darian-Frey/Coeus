#pragma once

#include <QSize>
#include <QString>

#include <expected>

namespace coeus {

// Draws a ```mermaid block to a PNG for reading mode (D-022, SPEC.md §Rendered
// blocks): a headless Chromium-family browser on the machine runs the
// vendored Mermaid script on a page made for the one diagram and prints it to
// a PDF of exactly its size, which Qt PDF turns into the picture. The browser
// gets a throwaway profile and no network, and loads only that page and the
// script. Blocking and slow (half a second): worker threads only.
class MermaidRenderer {
public:
    static constexpr int kVersion = 2; // with Mermaid's version (resources/mermaid/README.md)
    static constexpr double kScale = 2.0;
    static constexpr int kMaxSource = 20'000;

    // The look a diagram takes, to match Coeus's theme.
    struct Style {
        QString theme = QStringLiteral("neutral"); // Mermaid's: default, neutral, dark, forest, base
        QString background = QStringLiteral("#ffffff");
        QString themeVariables = QStringLiteral("{}"); // JSON, for theme "base"
        QString fontFamily;                            // empty for Mermaid's own
        QString key() const;                           // for the cache
    };

    // The browser to use, by the names Chrome and Chromium install under; empty if none.
    static QString browser();
    static bool available(QString* why = nullptr);
    // Draws `source` into `pngPath` with `browser` and the script at
    // `scriptPath`; the size to show it at, or why it could not.
    static std::expected<QSize, QString> render(const QString& source, const Style& style, const QString& scriptPath,
                                                const QString& browser, const QString& pngPath);
};

} // namespace coeus
