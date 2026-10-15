#include "render/MathsRenderer.hpp"

#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace coeus {

namespace {

constexpr int kTimeoutMs = 20'000;
// dvisvgm's size is LaTeX's 10 pt; shown beside text of `pointSize`, in pixels.
constexpr double kPixelsPerPoint = 96.0 / 72.0;

std::expected<void, QString> run(const QString& program, const QStringList& arguments, const QString& folder)
{
    QProcess process;
    process.setWorkingDirectory(folder);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, arguments);
    if (!process.waitForStarted(kTimeoutMs))
        return std::unexpected(QStringLiteral("%1 did not start").arg(program));
    if (!process.waitForFinished(kTimeoutMs)) {
        process.kill();
        process.waitForFinished();
        return std::unexpected(QStringLiteral("%1 took longer than %2 s").arg(program).arg(kTimeoutMs / 1000));
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return std::unexpected(QString::fromLocal8Bit(process.readAll()));
    return {};
}

// The first "! …" line of a LaTeX log: the error a writer can act on.
QString latexError(const QString& log)
{
    static const QRegularExpression kError(QStringLiteral(R"(^! (.+)$)"), QRegularExpression::MultilineOption);
    const auto m = kError.match(log);
    return m.hasMatch() ? m.captured(1).trimmed() : QStringLiteral("LaTeX could not typeset it");
}

} // namespace

bool MathsRenderer::available(QString* why)
{
    for (const char* tool : {"latex", "dvisvgm"}) {
        if (QStandardPaths::findExecutable(QString::fromLatin1(tool)).isEmpty()) {
            if (why)
                *why = QStringLiteral("no %1 on this machine: maths shows as code (install TeX Live and dvisvgm to draw it)")
                           .arg(QLatin1String(tool));
            return false;
        }
    }
    return true;
}

std::expected<QSize, QString> MathsRenderer::render(const QString& tex, bool display, const QColor& colour, double pointSize,
                                                    const QString& pngPath)
{
    if (tex.size() > kMaxSource)
        return std::unexpected(QStringLiteral("longer than %1 characters").arg(kMaxSource));
    QTemporaryDir folder;
    if (!folder.isValid())
        return std::unexpected(QStringLiteral("no temporary folder"));
    // article and amsmath come with every TeX; the page is cropped to the ink.
    const QString document = QStringLiteral("\\documentclass{article}\n\\usepackage{amsmath,amssymb}\n\\usepackage{color}\n"
                                            "\\definecolor{fg}{RGB}{%1,%2,%3}\n\\pagestyle{empty}\n\\begin{document}\n"
                                            "\\color{fg}$%4%5$\n\\end{document}\n")
                                 .arg(colour.red())
                                 .arg(colour.green())
                                 .arg(colour.blue())
                                 .arg(display ? QStringLiteral("\\displaystyle ") : QString(), tex);
    QFile source(folder.filePath(QStringLiteral("m.tex")));
    if (!source.open(QIODevice::WriteOnly))
        return std::unexpected(source.errorString());
    source.write(document.toUtf8());
    source.close();

    // No shell escape: a formula can never run a program.
    if (auto typeset = run(QStringLiteral("latex"), {QStringLiteral("-no-shell-escape"), QStringLiteral("-interaction=nonstopmode"),
                                                     QStringLiteral("-halt-on-error"), QStringLiteral("m.tex")},
                           folder.path());
        !typeset)
        return std::unexpected(latexError(typeset.error()));
    if (auto drawn = run(QStringLiteral("dvisvgm"), {QStringLiteral("--no-fonts"), QStringLiteral("--exact-bbox"),
                                                     QStringLiteral("--output=m.svg"), QStringLiteral("m.dvi")},
                         folder.path());
        !drawn)
        return std::unexpected(QStringLiteral("dvisvgm: %1").arg(drawn.error().trimmed().section(QLatin1Char('\n'), -1)));

    QImageReader reader(folder.filePath(QStringLiteral("m.svg")));
    const QSize natural = reader.size(); // LaTeX's 10 pt
    if (!natural.isValid())
        return std::unexpected(QStringLiteral("the drawing could not be read"));
    const double grow = pointSize / 10.0 * kPixelsPerPoint;
    const QSize shown = (QSizeF(natural) * grow).toSize();
    reader.setScaledSize((QSizeF(shown) * kScale).toSize());
    const QImage image = reader.read();
    if (image.isNull() || !image.save(pngPath, "PNG"))
        return std::unexpected(QStringLiteral("the drawing could not be saved"));
    return shown;
}

} // namespace coeus
