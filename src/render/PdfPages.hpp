#pragma once

#include <QSize>
#include <QString>

#include <expected>

namespace coeus {

// One page of a PDF as a PNG for reading mode (F-023, D-022), through Qt PDF.
// Blocking: worker threads only.
class PdfPages {
public:
    static constexpr int kVersion = 1;
    static constexpr double kScale = 2.0;
    static constexpr int kWidth = 640; // shown at most this wide, in pixels

    // Page `page` (1-based) of `pdfPath` into `pngPath`; the size to show it
    // at, or why not (a missing page names how many there are).
    static std::expected<QSize, QString> render(const QString& pdfPath, int page, const QString& pngPath);
};

} // namespace coeus
