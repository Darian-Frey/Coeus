// D-022: maths through LaTeX, Mermaid through a headless browser and PDF
// pages through Qt PDF, and the render service's cache. Cases needing a tool
// the machine lacks are skipped, as Coeus itself falls back to code.

#include "render/MathsRenderer.hpp"
#include "render/MermaidRenderer.hpp"
#include "render/PdfPages.hpp"
#include "ui/RenderService.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QPdfWriter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace coeus;

namespace {

// A PDF of `pages` pages, each saying its number.
void writePdf(const QString& path, int pages)
{
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A6));
    QPainter painter(&writer);
    for (int page = 1; page <= pages; ++page) {
        if (page > 1)
            writer.newPage();
        painter.drawText(QRect(0, 0, writer.width(), writer.height()), Qt::AlignCenter, QStringLiteral("Page %1").arg(page));
    }
}

QString script()
{
    return QStringLiteral(COEUS_SOURCE_DIR "/resources/mermaid/mermaid.min.js");
}

} // namespace

class TestRenderers : public QObject {
    Q_OBJECT

private slots:
    void mathsThroughLatex()
    {
        if (!MathsRenderer::available())
            QSKIP("No latex or dvisvgm here");
        QTemporaryDir dir;
        const QString png = dir.filePath(QStringLiteral("m.png"));
        const auto shown = MathsRenderer::render(QStringLiteral("\\frac{a}{b} + \\sqrt{x}"), true, QColor(0x12, 0x34, 0x56), 10.0, png);
        QVERIFY2(shown, shown ? "" : qPrintable(shown.error()));
        QVERIFY(shown->width() > 20 && shown->height() > 10);
        QCOMPARE(QImageReader(png).size(), *shown * 2); // drawn at twice the size shown
        // LaTeX's own words for a mistake, and never a shell escape.
        const auto broken = MathsRenderer::render(QStringLiteral("\\frac{a"), false, Qt::black, 10.0, dir.filePath(QStringLiteral("b.png")));
        QVERIFY(!broken);
        QVERIFY2(!broken.error().isEmpty() && !broken.error().contains(QLatin1String("\n")), qPrintable(broken.error()));
        const auto escape = MathsRenderer::render(QStringLiteral("x\\immediate\\write18{touch %1}").arg(dir.filePath(QStringLiteral("pwned"))),
                                                  false, Qt::black, 10.0, dir.filePath(QStringLiteral("e.png")));
        Q_UNUSED(escape);
        QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("pwned"))));
        QVERIFY(!MathsRenderer::render(QString(MathsRenderer::kMaxSource + 1, QLatin1Char('x')), false, Qt::black, 10.0, png));
    }

    void mermaidThroughABrowser()
    {
        if (!MermaidRenderer::available())
            QSKIP("No Chrome or Chromium here");
        QTemporaryDir dir;
        MermaidRenderer::Style style;
        style.theme = QStringLiteral("dark");
        style.background = QStringLiteral("#1b1e20");
        const QString png = dir.filePath(QStringLiteral("d.png"));
        const auto shown = MermaidRenderer::render(QStringLiteral("graph LR\n  a[Vault] --> b[(Index)]\n  b --> c[Search]\n"), style,
                                                   script(), MermaidRenderer::browser(), png);
        QVERIFY2(shown, shown ? "" : qPrintable(shown.error()));
        QVERIFY2(shown->width() > shown->height() && shown->width() < 800, "drawn at its own size, not the page's");
        const QImage image(png);
        QCOMPARE(image.size(), *shown * 2);
        QCOMPARE(image.pixelColor(2, 2).name(), QStringLiteral("#1b1e20")); // the theme's background
        // A diagram Mermaid cannot read still draws: its error, where the diagram would be.
        QVERIFY(MermaidRenderer::render(QStringLiteral("graph LR\n  a -->"), style, script(), MermaidRenderer::browser(),
                                        dir.filePath(QStringLiteral("e.png"))));
    }

    void pdfPagesThroughQtPdf()
    {
        QTemporaryDir dir;
        const QString pdf = dir.filePath(QStringLiteral("three.pdf"));
        writePdf(pdf, 3);
        const QString png = dir.filePath(QStringLiteral("p.png"));
        const auto shown = PdfPages::render(pdf, 2, png);
        QVERIFY2(shown, shown ? "" : qPrintable(shown.error()));
        QVERIFY(shown->height() > shown->width()); // A6, upright
        QVERIFY(shown->width() <= PdfPages::kWidth);
        QCOMPARE(QImage(png).pixelColor(1, 1), QColor(Qt::white)); // paper, whatever the theme
        QCOMPARE(PdfPages::render(pdf, 9, png).error(), QStringLiteral("no page 9; it has 3"));
        QFile notPdf(dir.filePath(QStringLiteral("x.pdf")));
        QVERIFY(notPdf.open(QIODevice::WriteOnly));
        notPdf.write("not a pdf");
        notPdf.close();
        QVERIFY(!PdfPages::render(notPdf.fileName(), 1, png));
    }

    void theServiceDrawsOnceAndRemembers()
    {
        QTemporaryDir vault;
        writePdf(vault.filePath(QStringLiteral("doc.pdf")), 2);
        RenderService service;
        QCOMPARE(service.pdfPage(vault.filePath(QStringLiteral("doc.pdf")), 1).url, QString()); // no vault: nothing drawn
        service.setVault(vault.path());
        QSignalSpy rendered(&service, &RenderService::rendered);

        // First asked: queued, answered empty; then drawn, cached and kept.
        RenderService::Picture page = service.pdfPage(vault.filePath(QStringLiteral("doc.pdf")), 2);
        QVERIFY(page.url.isEmpty() && page.error.isEmpty());
        service.waitForDone();
        QTRY_VERIFY(rendered.count() >= 1);
        page = service.pdfPage(vault.filePath(QStringLiteral("doc.pdf")), 2);
        QVERIFY(page.url.startsWith(QStringLiteral("file://") + vault.path() + QStringLiteral("/.coeus/render/pdf-")));
        QVERIFY(page.size.isValid());
        // A failure is remembered rather than tried on every redraw.
        QVERIFY(service.pdfPage(vault.filePath(QStringLiteral("doc.pdf")), 7).error.isEmpty()); // queued
        service.waitForDone();
        QTRY_COMPARE(service.pdfPage(vault.filePath(QStringLiteral("doc.pdf")), 7).error, QStringLiteral("no page 7; it has 2"));
        // An edited PDF is drawn again.
        QTest::qWait(1100); // a newer modification time
        writePdf(vault.filePath(QStringLiteral("doc.pdf")), 3);
        QVERIFY(service.pdfPage(vault.filePath(QStringLiteral("doc.pdf")), 2).url.isEmpty());
        service.waitForDone();

        if (MathsRenderer::available()) {
            QVERIFY(service.maths(QStringLiteral("x^2"), false).url.isEmpty());
            service.waitForDone();
            QTRY_VERIFY(!service.maths(QStringLiteral("x^2"), false).url.isEmpty());
            const int drawn = int(QDir(service.cacheFolder()).entryList({QStringLiteral("maths-*.png")}).size());
            QCOMPARE(drawn, 1);
            QApplication::setPalette(QPalette(QColor(0x40, 0x40, 0x40))); // another theme: another colour, drawn again
            QVERIFY(service.maths(QStringLiteral("x^2"), false).url.isEmpty());
            service.waitForDone();
            QTRY_COMPARE(int(QDir(service.cacheFolder()).entryList({QStringLiteral("maths-*.png")}).size()), 2);
        }
    }
};

QTEST_MAIN(TestRenderers)
#include "test_renderers.moc"
