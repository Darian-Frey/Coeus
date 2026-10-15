#include "render/MermaidRenderer.hpp"

#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonValue>
#include <QPdfDocument>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>

namespace coeus {

namespace {

constexpr int kTimeoutMs = 30'000;
constexpr double kPixelsPerPoint = 96.0 / 72.0; // the printed page is in points; the page was laid out in CSS pixels

// A JavaScript string literal for `text`, safe inside a <script> element.
QString scriptString(const QString& text)
{
    QString json = QString::fromUtf8(QJsonDocument::fromVariant(QVariantList{text}).toJson(QJsonDocument::Compact));
    json = json.mid(1, json.size() - 2); // ["…"] → "…"
    return json.replace(QStringLiteral("</"), QStringLiteral("<\\/"));
}

} // namespace

QString MermaidRenderer::Style::key() const
{
    return theme + QLatin1Char('|') + background + QLatin1Char('|') + themeVariables + QLatin1Char('|') + fontFamily;
}

QString MermaidRenderer::browser()
{
    for (const char* name : {"google-chrome", "google-chrome-stable", "chromium", "chromium-browser"}) {
        const QString path = QStandardPaths::findExecutable(QString::fromLatin1(name));
        if (!path.isEmpty())
            return path;
    }
    return {};
}

bool MermaidRenderer::available(QString* why)
{
    if (!browser().isEmpty())
        return true;
    if (why)
        *why = QStringLiteral("no Chrome or Chromium on this machine: Mermaid diagrams show as code");
    return false;
}

std::expected<QSize, QString> MermaidRenderer::render(const QString& source, const Style& style, const QString& scriptPath,
                                                      const QString& browser, const QString& pngPath)
{
    if (source.size() > kMaxSource)
        return std::unexpected(QStringLiteral("longer than %1 characters").arg(kMaxSource));
    QTemporaryDir folder;
    if (!folder.isValid())
        return std::unexpected(QStringLiteral("no temporary folder"));
    // The page sizes its printed page to the diagram; a Mermaid error is
    // printed instead, so it shows where the diagram would have been.
    const QString page = QStringLiteral(R"(<!doctype html><html><head><meta charset="utf-8"><style>
html, body { margin: 0; padding: 0; overflow: hidden; background: %1; -webkit-print-color-adjust: exact; print-color-adjust: exact; }
#error { font: 14px sans-serif; color: #c62828; padding: 8px; white-space: pre-wrap; }
</style></head><body><div id="out"></div>
<script src="%2"></script>
<script>
const config = { startOnLoad: false, securityLevel: 'strict', theme: %3, themeVariables: %4 };
if (%5) config.fontFamily = %5;
mermaid.initialize(config);
mermaid.render('diagram', %6).then(({ svg }) => {
  document.getElementById('out').innerHTML = svg;
  // Mermaid sizes its drawing to the page's width; draw it at its own size.
  const drawing = document.querySelector('#out svg');
  const view = drawing.viewBox.baseVal;
  if (view && view.width > 0) {
    drawing.style.maxWidth = 'none';
    drawing.setAttribute('width', view.width);
    drawing.setAttribute('height', view.height);
  }
  const box = drawing.getBoundingClientRect();
  const w = Math.ceil(box.width) + 2, h = Math.ceil(box.height) + 2;
  const size = document.createElement('style');
  size.textContent = '@page { size: ' + w + 'px ' + h + 'px; margin: 0 }';
  document.head.appendChild(size);
  document.body.style.width = w + 'px';
  document.body.style.height = h + 'px';
}).catch(error => {
  const box = document.getElementById('out');
  box.id = 'error';
  box.textContent = 'Mermaid: ' + (error && error.message ? error.message : error);
  const size = document.createElement('style');
  size.textContent = '@page { size: 600px 80px; margin: 0 }';
  document.head.appendChild(size);
});
</script></body></html>
)")
                             .arg(style.background, QUrl::fromLocalFile(scriptPath).toString(), scriptString(style.theme),
                                  style.themeVariables.isEmpty() ? QStringLiteral("{}") : style.themeVariables,
                                  scriptString(style.fontFamily), scriptString(source));
    QFile html(folder.filePath(QStringLiteral("diagram.html")));
    if (!html.open(QIODevice::WriteOnly))
        return std::unexpected(html.errorString());
    html.write(page.toUtf8());
    html.close();

    const QString pdf = folder.filePath(QStringLiteral("diagram.pdf"));
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(browser, {QStringLiteral("--headless=new"), QStringLiteral("--disable-gpu"), QStringLiteral("--no-first-run"),
                            QStringLiteral("--no-default-browser-check"), QStringLiteral("--disable-extensions"),
                            QStringLiteral("--user-data-dir=") + folder.filePath(QStringLiteral("profile")),
                            QStringLiteral("--allow-file-access-from-files"),
                            QStringLiteral("--host-resolver-rules=MAP * ~NOTFOUND"), // no network
                            QStringLiteral("--virtual-time-budget=10000"), QStringLiteral("--no-pdf-header-footer"),
                            QStringLiteral("--print-to-pdf=") + pdf, QUrl::fromLocalFile(html.fileName()).toString()});
    if (!process.waitForStarted(kTimeoutMs))
        return std::unexpected(QStringLiteral("%1 did not start").arg(browser));
    if (!process.waitForFinished(kTimeoutMs)) {
        process.kill();
        process.waitForFinished();
        return std::unexpected(QStringLiteral("the browser took longer than %1 s").arg(kTimeoutMs / 1000));
    }

    QPdfDocument document;
    if (document.load(pdf) != QPdfDocument::Error::None || document.pageCount() < 1)
        return std::unexpected(QStringLiteral("the browser drew nothing (%1)").arg(QString::fromLocal8Bit(process.readAll()).trimmed().left(200)));
    const QSizeF points = document.pagePointSize(0);
    const QSize shown = (points * kPixelsPerPoint).toSize();
    const QImage image = document.render(0, (QSizeF(shown) * kScale).toSize());
    if (image.isNull() || !image.save(pngPath, "PNG"))
        return std::unexpected(QStringLiteral("the drawing could not be saved"));
    return shown;
}

} // namespace coeus
