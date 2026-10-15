#include "ui/LivePreview.hpp"

#include "ui/Editor.hpp"
#include "ui/MarkdownHighlighter.hpp"
#include "ui/RenderService.hpp"
#include "ui/Theme.hpp"

#include <QDateTime>
#include <QFileInfo>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextBlock>
#include <QTimer>
#include <QUrl>

namespace coeus {

LivePreview::LivePreview(Editor* editor)
    : QObject(editor)
    , m_editor(editor)
    , m_scanTimer(new QTimer(this))
{
    m_scanTimer->setSingleShot(true);
    m_scanTimer->setInterval(kScanDelayMs);
    connect(m_scanTimer, &QTimer::timeout, this, [this] {
        rescan();
        update();
    });
    connect(m_editor->document(), &QTextDocument::contentsChanged, this, [this] {
        if (m_enabled)
            m_scanTimer->start();
    });
    connect(m_editor, &QPlainTextEdit::cursorPositionChanged, this, &LivePreview::update);
    connect(m_editor, &QPlainTextEdit::selectionChanged, this, &LivePreview::update);
    connect(&Theme::instance(), &Theme::changed, this, [this] {
        m_images.clear();
        m_pictures.clear();
        update();
    });
}

void LivePreview::setEnabled(bool enabled)
{
    if (enabled == m_enabled)
        return;
    m_enabled = enabled;
    if (enabled)
        rescan();
    update();
}

void LivePreview::setRenderService(RenderService* service)
{
    if (m_render)
        disconnect(m_render, nullptr, this, nullptr);
    m_render = service;
    if (m_render)
        connect(m_render, &RenderService::rendered, this, [this] {
            m_pictures.clear();
            update();
        });
}

void LivePreview::setImageResolver(std::function<QString(const QString&, bool)> resolver)
{
    m_resolve = std::move(resolver);
}

void LivePreview::rescan()
{
    m_blocks = LiveBlocks::scan(m_editor->toPlainText());
}

bool LivePreview::closesFence(int line) const
{
    const QString text = m_editor->document()->findBlockByNumber(line).text().trimmed();
    return text.size() >= 3 && (text.count(QLatin1Char('`')) == text.size() || text.count(QLatin1Char('~')) == text.size());
}

QImage LivePreview::load(const QString& path)
{
    const QFileInfo file(path);
    const QString key = path + QLatin1Char('|') + QString::number(file.lastModified().toMSecsSinceEpoch());
    if (const auto it = m_images.constFind(key); it != m_images.cend())
        return *it;
    QImage image(path);
    m_images.insert(key, image);
    return image;
}

std::optional<LivePreview::Drawn> LivePreview::pictureFor(const LiveBlock& block)
{
    const int room = std::max(120, m_editor->viewport()->width() - 2 * kPictureMargin - 24);
    const QString key = QStringLiteral("%1|%2|%3|%4").arg(int(block.kind)).arg(room).arg(block.wiki).arg(block.source);
    if (const auto it = m_pictures.constFind(key); it != m_pictures.cend())
        return *it;
    std::optional<Drawn> drawn = draw(block, room);
    // Not cached while it is being drawn: the landing clears the cache anyway.
    if (drawn)
        m_pictures.insert(key, drawn);
    return drawn;
}

std::optional<LivePreview::Drawn> LivePreview::draw(const LiveBlock& block, int room)
{
    QString path;
    QSize size;
    switch (block.kind) {
    case LiveBlock::Kind::Maths:
    case LiveBlock::Kind::Mermaid: {
        if (!m_render)
            return std::nullopt;
        const RenderService::Picture picture =
            block.kind == LiveBlock::Kind::Maths ? m_render->maths(block.source, true) : m_render->mermaid(block.source);
        if (picture.url.isEmpty())
            return std::nullopt; // being drawn, or cannot be: the source shows
        path = QUrl(picture.url).toLocalFile();
        size = picture.size;
        break;
    }
    case LiveBlock::Kind::Picture: {
        if (!m_resolve)
            return std::nullopt;
        const QString target = block.source.section(QLatin1Char('#'), 0, 0);
        const QString fragment = block.source.section(QLatin1Char('#'), 1);
        const QString url = m_resolve(target, block.wiki);
        if (url.isEmpty())
            return std::nullopt;
        const QString file = QUrl(url).toLocalFile();
        if (QFileInfo(file).suffix().compare(QLatin1String("pdf"), Qt::CaseInsensitive) == 0) {
            if (!m_render)
                return std::nullopt;
            static const QRegularExpression kPage(QStringLiteral(R"(^page=(\d+)$)"));
            const auto m = kPage.match(fragment);
            const RenderService::Picture picture = m_render->pdfPage(file, m.hasMatch() ? m.captured(1).toInt() : 1);
            if (picture.url.isEmpty())
                return std::nullopt;
            path = QUrl(picture.url).toLocalFile();
            size = picture.size;
        } else {
            path = file;
        }
        break;
    }
    default:
        return std::nullopt;
    }
    const QImage image = load(path);
    if (image.isNull())
        return std::nullopt;
    if (!size.isValid())
        size = image.size() / image.devicePixelRatio();
    // Never wider than the editor: a wide picture is shown smaller.
    if (size.width() > room)
        size = QSize(room, int(double(size.height()) * room / size.width()));
    return Drawn{image, size};
}

void LivePreview::update()
{
    MarkdownHighlighter* highlighter = m_editor->highlighter();
    QTextDocument* document = m_editor->document();
    QSet<int> folded;
    QHash<int, int> tall;
    QHash<int, Drawn> drawn;
    QList<QTextEdit::ExtraSelection> tint;
    if (m_enabled) {
        // The cursor's lines, or the selection's.
        const QTextCursor cursor = m_editor->textCursor();
        m_revealFirst = document->findBlock(cursor.selectionStart()).blockNumber();
        m_revealLast = document->findBlock(cursor.selectionEnd()).blockNumber();
        for (const LiveBlock& block : std::as_const(m_blocks)) {
            const bool revealed = block.lastLine >= m_revealFirst && block.firstLine <= m_revealLast;
            if (block.kind == LiveBlock::Kind::Code || (block.kind == LiveBlock::Kind::Mermaid && revealed)) {
                // Code reads as a tinted block; its fences fold away when not edited.
                for (int line = block.firstLine; line <= block.lastLine; ++line) {
                    QTextEdit::ExtraSelection selection;
                    selection.cursor = QTextCursor(document->findBlockByNumber(line));
                    selection.format.setBackground(Theme::instance().colour(Theme::Colour::Tint));
                    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
                    tint.append(selection);
                }
                if (!revealed && block.lastLine > block.firstLine) {
                    folded.insert(block.firstLine);
                    if (closesFence(block.lastLine))
                        folded.insert(block.lastLine);
                }
                continue;
            }
            if (revealed)
                continue;
            if (block.kind == LiveBlock::Kind::Frontmatter) {
                for (int line = block.firstLine; line <= block.lastLine; ++line)
                    folded.insert(line);
                continue;
            }
            if (const auto picture = pictureFor(block)) {
                drawn.insert(block.firstLine, *picture);
                tall.insert(block.firstLine, picture->size.height() + 2 * kPictureMargin);
                for (int line = block.firstLine + 1; line <= block.lastLine; ++line)
                    folded.insert(line);
            }
        }
    } else {
        m_revealFirst = m_revealLast = -1;
    }

    // Fold and unfold only the lines that change, then lay them out again.
    const QSet<int> changed = (folded - m_folded) + (m_folded - folded);
    for (int line : changed) {
        QTextBlock block = document->findBlockByNumber(line);
        if (!block.isValid())
            continue;
        block.setVisible(!folded.contains(line));
        document->markContentsDirty(block.position(), block.length());
    }
    m_folded = folded;
    m_drawn = drawn;
    highlighter->setRevealed(m_revealFirst, m_revealLast);
    highlighter->setTallLines(tall);
    m_editor->setLiveSelections(tint);
    m_editor->viewport()->update();
}

void LivePreview::paint(QPainter& painter, const QRect& exposed)
{
    if (!m_enabled)
        return;
    for (auto it = m_drawn.cbegin(); it != m_drawn.cend(); ++it) {
        const QTextBlock block = m_editor->document()->findBlockByNumber(it.key());
        if (!block.isValid() || !block.isVisible())
            continue;
        const QRectF line = m_editor->blockRect(block);
        const QRect target(int(line.left()) + kPictureMargin, int(line.top()) + kPictureMargin, it->size.width(), it->size.height());
        if (!target.intersects(exposed))
            continue;
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(target, it->image);
    }
}

} // namespace coeus
