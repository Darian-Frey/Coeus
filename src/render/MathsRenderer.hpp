#pragma once

#include <QColor>
#include <QSize>
#include <QString>

#include <expected>

namespace coeus {

// Draws a LaTeX formula to a PNG for reading mode (D-022, SPEC.md §Rendered
// blocks) through the machine's `latex` and `dvisvgm`, if it has them. The PNG
// is drawn at kScale times the size it is shown at, for sharp text on any
// screen. Blocking and slow (a tenth of a second): worker threads only.
class MathsRenderer {
public:
    static constexpr int kVersion = 1;   // raise to draw every cached formula again
    static constexpr double kScale = 2.0;
    static constexpr int kMaxSource = 4000;

    // Whether `latex` and `dvisvgm` are on PATH; if not, `why` says which is missing.
    static bool available(QString* why = nullptr);
    // Draws `tex` (without its $ delimiters) in `colour` at about `pointSize`,
    // as a display formula or inline, into `pngPath`; the size to show it at,
    // or LaTeX's own error.
    static std::expected<QSize, QString> render(const QString& tex, bool display, const QColor& colour, double pointSize,
                                                const QString& pngPath);
};

} // namespace coeus
