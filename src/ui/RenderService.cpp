#include "ui/RenderService.hpp"

#include "index/ContentHash.hpp"
#include "render/MathsRenderer.hpp"
#include "render/PdfPages.hpp"
#include "ui/Theme.hpp"
#include "vault/AtomicWriter.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QImageReader>
#include <QPointer>
#include <QThreadPool>
#include <QUrl>

namespace coeus {

namespace {

constexpr const char* kScriptResource = ":/mermaid/mermaid.min.js";
constexpr const char* kScriptName = "mermaid-11.4.1.min.js";

QString hex(const QByteArray& source)
{
    return QString::fromLatin1(ContentHash::of(source).toHex());
}

} // namespace

RenderService::RenderService(QObject* parent)
    : QObject(parent)
    , m_pool(new QThreadPool(this))
{
    m_pool->setMaxThreadCount(2); // a browser at a time is plenty
}

RenderService::~RenderService()
{
    m_pool->waitForDone();
}

void RenderService::setVault(const QString& vaultPath)
{
    m_pool->waitForDone();
    m_pending.clear();
    m_said.clear();
    m_folder = vaultPath.isEmpty() ? QString() : vaultPath + QStringLiteral("/.coeus/render");
}

void RenderService::waitForDone()
{
    m_pool->waitForDone();
    QCoreApplication::sendPostedEvents(this); // deliver what the workers reported
}

RenderService::Picture RenderService::lookup(const QString& kind, const QByteArray& keySource,
                                             const std::function<std::expected<QSize, QString>(const QString& png)>& draw)
{
    if (m_folder.isEmpty())
        return {};
    const QString key = kind + QLatin1Char('-') + hex(keySource);
    const QString png = m_folder + QLatin1Char('/') + key + QStringLiteral(".png");
    const QString failed = m_folder + QLatin1Char('/') + key + QStringLiteral(".error");
    if (QFileInfo::exists(png)) {
        // Drawn at twice the size it is shown at (each renderer's kScale).
        const QSize pixels = QImageReader(png).size();
        return {QUrl::fromLocalFile(png).toString(), pixels / 2, {}};
    }
    if (QFile file(failed); file.open(QIODevice::ReadOnly))
        return {{}, {}, QString::fromUtf8(file.readAll())};
    if (m_pending.contains(key))
        return {};
    m_pending.insert(key);
    QDir().mkpath(m_folder);
    QPointer<RenderService> self(this);
    m_pool->start([self, key, png, failed, draw] {
        const QString temporary = png + QStringLiteral(".part");
        const auto drawn = draw(temporary);
        if (drawn) {
            QFile::remove(png);
            QFile::rename(temporary, png); // whole or not at all
        } else {
            QFile::remove(temporary);
            (void)AtomicWriter::write(failed, drawn.error().toUtf8());
        }
        QMetaObject::invokeMethod(qApp, [self, key] {
            if (!self)
                return;
            self->m_pending.remove(key);
            emit self->rendered();
        });
    });
    return {};
}

RenderService::Picture RenderService::maths(const QString& tex, bool display)
{
    QString why;
    if (!MathsRenderer::available(&why)) {
        if (!m_said.contains(QStringLiteral("maths")) && !m_folder.isEmpty()) {
            m_said.insert(QStringLiteral("maths"));
            emit unavailable(why);
        }
        return {};
    }
    const QColor colour = QApplication::palette().color(QPalette::Text);
    const double points = QApplication::font().pointSizeF() > 0 ? QApplication::font().pointSizeF() : 10.0;
    const QByteArray keySource = QStringLiteral("%1|%2|%3|%4|%5").arg(MathsRenderer::kVersion).arg(display).arg(colour.name()).arg(points).arg(tex).toUtf8();
    return lookup(QStringLiteral("maths"), keySource, [tex, display, colour, points](const QString& png) {
        return MathsRenderer::render(tex, display, colour, points, png);
    });
}

MermaidRenderer::Style RenderService::mermaidStyle() const
{
    const Theme& theme = Theme::instance();
    MermaidRenderer::Style style;
    style.background = QApplication::palette().color(QPalette::Base).name();
    if (theme.mode() == Theme::Mode::Cassette) {
        // Amber phosphor on warm black, as the rest of Cassette.
        style.theme = QStringLiteral("base");
        style.fontFamily = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
        style.themeVariables = QStringLiteral(
            R"({"background":"#0f0b06","primaryColor":"#2a1d0c","primaryTextColor":"#ffb000","primaryBorderColor":"#ff6a00",)"
            R"("lineColor":"#ff8c1a","secondaryColor":"#1b140b","tertiaryColor":"#14100a","textColor":"#ffb000",)"
            R"("mainBkg":"#2a1d0c","nodeBorder":"#ff6a00","clusterBkg":"#1b140b","edgeLabelBackground":"#14100a"})");
    } else {
        style.theme = theme.isDark() ? QStringLiteral("dark") : QStringLiteral("neutral");
    }
    return style;
}

QString RenderService::scriptPath()
{
    // The browser needs a file: the vendored script, copied out once.
    const QString path = m_folder + QLatin1Char('/') + QLatin1String(kScriptName);
    if (!QFileInfo::exists(path)) {
        QDir().mkpath(m_folder);
        QFile resource(QString::fromLatin1(kScriptResource));
        if (resource.open(QIODevice::ReadOnly))
            (void)AtomicWriter::write(path, resource.readAll());
    }
    return path;
}

RenderService::Picture RenderService::mermaid(const QString& source)
{
    QString why;
    if (!MermaidRenderer::available(&why)) {
        if (!m_said.contains(QStringLiteral("mermaid")) && !m_folder.isEmpty()) {
            m_said.insert(QStringLiteral("mermaid"));
            emit unavailable(why);
        }
        return {};
    }
    if (m_folder.isEmpty())
        return {};
    const MermaidRenderer::Style style = mermaidStyle();
    const QByteArray keySource = QStringLiteral("%1|%2|%3|%4").arg(MermaidRenderer::kVersion).arg(QLatin1String(kScriptName), style.key(), source).toUtf8();
    const QString script = scriptPath();
    const QString browser = MermaidRenderer::browser();
    return lookup(QStringLiteral("mermaid"), keySource, [source, style, script, browser](const QString& png) {
        return MermaidRenderer::render(source, style, script, browser, png);
    });
}

RenderService::Picture RenderService::pdfPage(const QString& pdfPath, int page)
{
    const QFileInfo file(pdfPath);
    if (!file.isFile())
        return {{}, {}, QStringLiteral("no such file")};
    // The file's identity as it is now: an edited PDF is drawn again.
    const QByteArray keySource = QStringLiteral("%1|%2|%3|%4|%5")
                                     .arg(PdfPages::kVersion)
                                     .arg(file.absoluteFilePath())
                                     .arg(file.size())
                                     .arg(file.lastModified().toMSecsSinceEpoch())
                                     .arg(page)
                                     .toUtf8();
    return lookup(QStringLiteral("pdf"), keySource, [pdfPath, page](const QString& png) { return PdfPages::render(pdfPath, page, png); });
}

} // namespace coeus
