#include "ui/NoteView.hpp"

#include "parser/HtmlRenderer.hpp"
#include "ui/Editor.hpp"

#include <QButtonGroup>
#include <QDir>
#include <QFileInfo>
#include <QApplication>
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

    m_sourceButton = modeButton(tr("Source"), header);
    QToolButton* live = modeButton(tr("Live"), header);
    live->setEnabled(false);
    live->setToolTip(tr("Live preview arrives in Phase 4"));
    m_readingButton = modeButton(tr("Reading"), header);
    auto* group = new QButtonGroup(this);
    group->setExclusive(true);
    for (QToolButton* button : {m_sourceButton, live, m_readingButton}) {
        group->addButton(button);
        row->addWidget(button);
    }
    m_sourceButton->setChecked(true);
    connect(m_sourceButton, &QToolButton::clicked, this, [this] { setMode(Mode::Source); });
    connect(m_readingButton, &QToolButton::clicked, this, [this] { setMode(Mode::Reading); });

    m_reader->setOpenLinks(false);
    m_reader->setOpenExternalLinks(false);
    m_reader->setFrameShape(QFrame::NoFrame);
    m_reader->document()->setDefaultStyleSheet(styleSheet());
    m_reader->document()->setDocumentMargin(16);
    connect(m_reader, &QTextBrowser::anchorClicked, this, &NoteView::linkActivated);

    m_stack->addWidget(m_editor);
    m_stack->addWidget(m_reader);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header);
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
    m_readingButton->setChecked(mode == Mode::Reading);
    if (m_mode == mode)
        return;
    m_mode = mode;
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

void NoteView::toggleMode()
{
    setMode(m_mode == Mode::Source ? Mode::Reading : Mode::Source);
}

void NoteView::render()
{
    HtmlRenderer::Options options;
    options.resolveImage = [this](const QString& target, bool wiki) { return resolveImage(target, wiki); };
    const int scroll = m_reader->verticalScrollBar()->value();
    m_reader->setHtml(HtmlRenderer::render(m_editor->toPlainText().toUtf8(), options));
    m_reader->verticalScrollBar()->setValue(scroll);
}

void NoteView::goToHeading(int line, const QString& text, int occurrence, int index)
{
    if (m_mode == Mode::Source) {
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

QString NoteView::styleSheet() const
{
    const QPalette palette = QGuiApplication::palette();
    const bool dark = palette.color(QPalette::Base).lightness() < 128;
    const QString accent = dark ? QStringLiteral("#5fc4b8") : QStringLiteral("#1f7a70");
    const QString dim = palette.color(QPalette::PlaceholderText).name();
    const QString tint = dark ? QStringLiteral("#2a2f33") : QStringLiteral("#f1f3f4");
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
               "p.problem { color: #c0392b; }")
        .arg(accent, dim, tint, dark ? QStringLiteral("#7a6a1c") : QStringLiteral("#ffec8a"));
}

} // namespace coeus
