#include "render/PdfPages.hpp"

#include <QImage>
#include <QPainter>
#include <QPdfDocument>

#include <algorithm>

namespace coeus {

std::expected<QSize, QString> PdfPages::render(const QString& pdfPath, int page, const QString& pngPath)
{
    QPdfDocument document;
    if (const auto error = document.load(pdfPath); error != QPdfDocument::Error::None)
        return std::unexpected(error == QPdfDocument::Error::IncorrectPassword ? QStringLiteral("it is password-protected")
                                                                               : QStringLiteral("it is not a PDF Coeus can read"));
    if (page < 1 || page > document.pageCount())
        return std::unexpected(QStringLiteral("no page %1; it has %2").arg(page).arg(document.pageCount()));
    const QSizeF points = document.pagePointSize(page - 1);
    const double width = std::min<double>(kWidth, points.width() * 96.0 / 72.0);
    const QSize shown = QSizeF(width, width * points.height() / points.width()).toSize();
    QImage image(QSizeF(QSizeF(shown) * kScale).toSize(), QImage::Format_ARGB32);
    image.fill(Qt::white); // a page is paper, whatever the theme
    const QImage drawn = document.render(page - 1, image.size());
    if (drawn.isNull())
        return std::unexpected(QStringLiteral("the page could not be drawn"));
    QImage composed = image;
    {
        QPainter painter(&composed);
        painter.drawImage(0, 0, drawn);
    }
    if (!composed.save(pngPath, "PNG"))
        return std::unexpected(QStringLiteral("the drawing could not be saved"));
    return shown;
}

} // namespace coeus
