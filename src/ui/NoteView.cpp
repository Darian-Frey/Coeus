#include "ui/NoteView.hpp"

#include "index/IndexQueries.hpp"

#include "ui/LivePreview.hpp"
#include "ui/RenderService.hpp"
#include "ui/Theme.hpp"

#include "parser/HtmlRenderer.hpp"
#include "ui/Editor.hpp"
#include "ui/PropertiesPanel.hpp"

#include <QButtonGroup>
#include <QDir>
#include <QFileInfo>
#include <QApplication>

#include <algorithm>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QScrollBar>
#include <QAbstractTextDocumentLayout>
#include <QStackedWidget>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace coeus {

namespace {

QToolButton* modeButton(const QString& text, QWidget* parent)
{
    auto* button = new QToolButton(parent);
    button->setText(text);
    button->setCheckable(true);
    button->setAutoRaise(true);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    return button;
}

} // namespace

NoteView::NoteView(const Vault& root, const QString& absolutePath, const QString& displayPath, QWidget* parent)
    : QWidget(parent)
    , m_root(root)
    , m_path(absolutePath)
    , m_displayPath(displayPath)
    , m_editor(new Editor(root, absolutePath))
    , m_reader(new QTextBrowser)
    , m_stack(new QStackedWidget(this))
    , m_renderTimer(new QTimer(this))
{
    // Header: "root / folder / note.md" and the mode switch (mockup).
    auto* header = new QWidget(this);
    auto* row = new QHBoxLayout(header);
    row->setContentsMargins(8, 2, 4, 2);
    auto* crumbs = new QLabel(displayPath.split(QLatin1Char('/')).join(QStringLiteral("  /  ")), header);
    crumbs->setForegroundRole(QPalette::PlaceholderText);
    crumbs->setTextInteractionFlags(Qt::TextSelectableByMouse);
    row->addWidget(crumbs, 1);
    m_starButton = new QToolButton(header);
    m_starButton->setAutoRaise(true);
    connect(m_starButton, &QToolButton::clicked, this, &NoteView::starToggled);
    row->addWidget(m_starButton);
    setStarred(false);

    m_sourceButton = modeButton(tr("Source"), header);
    m_liveButton = modeButton(tr("Live"), header);
    m_liveButton->setToolTip(tr("Live preview: the note as it reads, its Markdown shown on the line you are editing"));
    QToolButton* live = m_liveButton;
    m_readingButton = modeButton(tr("Reading"), header);
    auto* group = new QButtonGroup(this);
    group->setExclusive(true);
    for (QToolButton* button : {m_sourceButton, live, m_readingButton}) {
        group->addButton(button);
        row->addWidget(button);
    }
    m_sourceButton->setChecked(true);
    connect(m_sourceButton, &QToolButton::clicked, this, [this] { setMode(Mode::Source); });
    connect(m_liveButton, &QToolButton::clicked, this, [this] { setMode(Mode::Live); });
    connect(m_readingButton, &QToolButton::clicked, this, [this] { setMode(Mode::Reading); });

    m_reader->setOpenLinks(false);
    m_reader->setOpenExternalLinks(false);
    m_reader->setFrameShape(QFrame::NoFrame);
    m_reader->document()->setDefaultStyleSheet(styleSheet());
    Theme::followTextZoom(m_reader, false); // the interface's font, at the text zoom
    connect(&Theme::instance(), &Theme::textZoomChanged, this, [this] {
        if (mode() == Mode::Reading)
            render(); // formulas and diagrams at the new size
    });
    connect(&Theme::instance(), &Theme::changed, this, [this] {
        m_reader->document()->setDefaultStyleSheet(styleSheet());
        if (mode() == Mode::Reading)
            render();
    });
    m_reader->document()->setDocumentMargin(16);
    connect(m_reader, &QTextBrowser::anchorClicked, this, &NoteView::linkActivated);
    connect(m_editor, &Editor::linkActivated, this, &NoteView::linkActivated); // Ctrl+click (F-003)
    m_editor->live()->setImageResolver([this](const QString& target, bool wiki) { return resolveImage(target, wiki); });
    // Tables, callouts and note embeds in live preview are drawn as reading mode draws them (F-003).
    m_editor->live()->setHtmlRenderer(
        [this](const QByteArray& markdown) {
            const int room = std::max(200, m_editor->viewport()->width() - 4 * LivePreview::kPictureMargin - 24);
            return HtmlRenderer::render(markdown, readingOptions(room));
        },
        [this] { return styleSheet(); });

    m_stack->addWidget(m_editor);
    m_stack->addWidget(m_reader);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header);
    // Properties above the note in source mode (F-008); reading mode renders its own table.
    m_properties = new PropertiesPanel(m_editor, this);
    layout->addWidget(m_properties);
    layout->addWidget(m_stack, 1);

    // The reading view follows the buffer while it is showing.
    m_renderTimer->setSingleShot(true);
    m_renderTimer->setInterval(kRenderDelayMs);
    connect(m_renderTimer, &QTimer::timeout, this, &NoteView::render);
    connect(m_editor, &QPlainTextEdit::textChanged, this, [this] {
        if (m_mode == Mode::Reading)
            m_renderTimer->start();
    });
    setFocusProxy(m_editor);
}

void NoteView::setMode(Mode mode)
{
    m_sourceButton->setChecked(mode == Mode::Source);
    m_liveButton->setChecked(mode == Mode::Live);
    m_readingButton->setChecked(mode == Mode::Reading);
    if (mode != Mode::Reading)
        m_editMode = mode;
    m_editor->setLive(mode == Mode::Live);
    if (m_mode == mode)
        return;
    m_mode = mode;
    m_properties->setVisible(mode != Mode::Reading);
    if (mode == Mode::Reading) {
        render();
        m_stack->setCurrentWidget(m_reader);
        setFocusProxy(m_reader);
    } else {
        m_stack->setCurrentWidget(m_editor);
        setFocusProxy(m_editor);
    }
    if (hasFocus() || isAncestorOf(QApplication::focusWidget()))
        focusProxy()->setFocus();
    emit modeChanged(mode);
}

void NoteView::showEditor()
{
    if (m_mode == Mode::Reading)
        setMode(m_editMode);
}

void NoteView::toggleMode()
{
    setMode(m_mode == Mode::Reading ? m_editMode : Mode::Reading);
}

HtmlRenderer::Options NoteView::readingOptions(int room) const
{
    HtmlRenderer::Options options;
    options.resolveImage = [this](const QString& target, bool wiki) { return resolveImage(target, wiki); };
    if (!m_indexPath.isEmpty())
        options.resolveNote = [this](const QString& target, const QString& fromPath) { return resolveNote(target, fromPath); };
    options.notePath = m_path;
    if (m_render) {
        // Never wider than `room`: a wide diagram is shown smaller, never cut off.
        // A diagram is text, so it follows the text zoom (maths is drawn at it already).
        const auto picture = [room](const RenderService::Picture& p, double scale = 1.0) {
            QSize size = p.size * scale;
            if (size.width() > room)
                size = QSize(room, int(double(size.height()) * room / size.width()));
            return HtmlRenderer::Picture{p.url, size.width(), size.height(), p.error};
        };
        options.renderMaths = [this, picture](const QString& tex, bool display) { return picture(m_render->maths(tex, display)); };
        options.renderMermaid = [this, picture](const QString& source) {
            return picture(m_render->mermaid(source), Theme::instance().textZoom());
        };
        options.renderPdfPage = [this, picture](const QString& url, int page) {
            return picture(m_render->pdfPage(QUrl(url).toLocalFile(), page));
        };
    }
    options.embedAccent = Theme::instance().colour(Theme::Colour::Accent).name();
    return options;
}

void NoteView::render()
{
    const int room = std::max(200, m_reader->viewport()->width() - 2 * int(m_reader->document()->documentMargin()) - 8);
    const int scroll = m_reader->verticalScrollBar()->value();
    m_reader->setHtml(HtmlRenderer::render(m_editor->toPlainText().toUtf8(), readingOptions(room)));
    m_reader->verticalScrollBar()->setValue(scroll);
}

void NoteView::goToHeading(int line, const QString& text, int occurrence, int index)
{
    if (m_mode != Mode::Reading) { // Source and Live: the editor
        const QTextBlock block = m_editor->document()->findBlockByNumber(line - 1);
        if (!block.isValid())
            return;
        m_editor->setTextCursor(QTextCursor(block));
        m_editor->verticalScrollBar()->setValue(block.firstLineNumber());
        m_editor->setFocus();
        return;
    }
    // Rendered headings carry their level in the block format.
    QTextBlock byText;
    QTextBlock byIndex;
    int seenText = 0;
    int seen = 0;
    for (QTextBlock block = m_reader->document()->begin(); block.isValid(); block = block.next()) {
        if (block.blockFormat().headingLevel() <= 0)
            continue;
        if (seen++ == index)
            byIndex = block;
        if (block.text().simplified() == text && seenText++ == occurrence)
            byText = block;
    }
    const QTextBlock target = byText.isValid() ? byText : byIndex;
    if (!target.isValid())
        return;
    const qreal top = m_reader->document()->documentLayout()->blockBoundingRect(target).top();
    m_reader->verticalScrollBar()->setValue(int(top));
}

void NoteView::setPropertyContext(const QString& vaultPath, bool inVault)
{
    m_properties->setContext(vaultPath, m_indexPath, m_displayPath, inVault);
}

void NoteView::setRenderService(RenderService* service)
{
    m_render = service;
    m_editor->live()->setRenderService(service);
}

void NoteView::refreshReading()
{
    if (m_mode == Mode::Reading)
        render();
    else if (m_mode == Mode::Live)
        m_editor->live()->invalidate(); // its tables and embeds are drawn from the same renderer
}

HtmlRenderer::Embedded NoteView::resolveNote(const QString& target, const QString& fromPath) const
{
    // As a click on the link would: the index's resolution, never a guess (AV-006).
    const auto found = IndexQueries::wikilinkTarget(m_indexPath, fromPath, target);
    if (!found)
        return {{}, {}, {}, found.error()};
    if (!*found)
        return {{}, {}, {}, tr("not indexed yet")};
    if ((*found)->ambiguous)
        return {{}, {}, {}, tr("matches more than one note")};
    const QString path = (*found)->absolutePath;
    if (path.isEmpty())
        return {{}, {}, {}, tr("no such note")};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {{}, {}, {}, tr("cannot be read")};
    return {path, QFileInfo(path).completeBaseName(), file.readAll(), {}};
}

QString NoteView::resolveImage(const QString& target, bool wiki) const
{
    // Markdown images are relative to the note; ![[embeds]] also try the root.
    const QString folder = QFileInfo(m_path).absolutePath();
    QStringList candidates{QDir::cleanPath(folder + QLatin1Char('/') + target)};
    if (wiki)
        candidates.append(QDir::cleanPath(m_root.rootPath() + QLatin1Char('/') + target));
    for (const QString& candidate : std::as_const(candidates)) {
        if (QFileInfo(candidate).isFile() && m_root.contains(candidate))
            return QUrl::fromLocalFile(candidate).toString();
    }
    return {};
}

void NoteView::setStarred(bool starred)
{
    m_starButton->setText(starred ? QStringLiteral("★") : QStringLiteral("☆"));
    m_starButton->setToolTip(starred ? tr("Bookmarked: click to remove (Ctrl+Alt+S)") : tr("Bookmark this note (Ctrl+Alt+S)"));
    m_starButton->setProperty("starred", starred);
}

QString NoteView::styleSheet() const
{
    const Theme& theme = Theme::instance();
    const QString accent = theme.colour(Theme::Colour::Accent).name();
    const QString dim = QGuiApplication::palette().color(QPalette::PlaceholderText).name();
    const QString tint = theme.colour(Theme::Colour::Tint).name();
    return QStringLiteral(
               "a { color: %1; text-decoration: none; }"
               "a.tag { color: %1; }"
               "code { font-family: monospace; background-color: %3; }"
               "pre { font-family: monospace; background-color: %3; padding: 8px; }"
               "blockquote { color: %2; margin-left: 12px; }"
               "span.highlight { background-color: %4; }"
               "span.missing { color: %2; }"
               "table.properties { margin-bottom: 12px; }"
               "td.key { color: %2; padding-right: 16px; }"
               "table.grid th { border-bottom: 1px solid %2; }"
               "table.callout { background-color: %3; margin: 6px 0; }"
               "p.problem { color: %5; }"
               "table.embed { background-color: %3; margin: 6px 0; }"
               "p.embed-title { margin-bottom: 4px; }"
               "a.attachment { text-decoration: none; }"
               "span.kw { color: %6; font-weight: bold; }"
               "span.ty { color: %7; }"
               "span.st { color: %8; }"
               "span.cm { color: %2; font-style: italic; }"
               "span.nu { color: %9; }")
        .arg(accent, dim, tint, theme.colour(Theme::Colour::Highlight).name(), theme.colour(Theme::Colour::Error).name(),
             theme.colour(Theme::Colour::CodeKeyword).name(), theme.colour(Theme::Colour::CodeType).name(),
             theme.colour(Theme::Colour::CodeString).name(), theme.colour(Theme::Colour::CodeNumber).name());
}

} // namespace coeus
