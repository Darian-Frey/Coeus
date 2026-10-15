#include "ui/Editor.hpp"

#include "index/ContentHash.hpp"
#include "parser/Transclusion.hpp"
#include "ui/MarkdownHighlighter.hpp"
#include "ui/NoteCatalogue.hpp"
#include "ui/Theme.hpp"
#include "ui/WikilinkCompleter.hpp"

#include <QContextMenuEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QRandomGenerator>
#include <QScrollBar>
#include <QSet>
#include <QTextBlock>
#include <QTimer>
#include <QToolTip>

namespace coeus {

namespace {

constexpr int kBlameDelayMs = 600; // after the last keystroke

} // namespace

// The strip left of the text that shows blame (F-021); Editor does the work.
class BlameGutter : public QWidget {
public:
    explicit BlameGutter(Editor* editor)
        : QWidget(editor)
        , m_editor(editor)
    {
    }
    QSize sizeHint() const override { return {m_editor->blameGutterWidth(), 0}; }

protected:
    void paintEvent(QPaintEvent* event) override { m_editor->paintBlame(event); }
    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::ToolTip) {
            const auto* help = static_cast<QHelpEvent*>(event);
            const QTextCursor cursor = m_editor->cursorForPosition(QPoint(0, help->pos().y()));
            const QString text = m_editor->blameToolTip(cursor.blockNumber());
            if (text.isEmpty())
                QToolTip::hideText();
            else
                QToolTip::showText(help->globalPos(), text, this);
            return true;
        }
        return QWidget::event(event);
    }

private:
    Editor* m_editor;
};

Editor::Editor(const Vault& root, const QString& absolutePath, QWidget* parent)
    : QPlainTextEdit(parent)
    , m_root(root)
    , m_path(absolutePath)
    , m_highlighter(new MarkdownHighlighter(document()))
{
    Theme::setMonospace(this);
    setLineWrapMode(QPlainTextEdit::WidgetWidth);
    setTabChangesFocus(false);
    connect(document(), &QTextDocument::modificationChanged, this, &Editor::modificationChanged);

    m_blameGutter = new BlameGutter(this);
    m_blameGutter->hide();
    m_blameTimer = new QTimer(this);
    m_blameTimer->setSingleShot(true);
    m_blameTimer->setInterval(kBlameDelayMs);
    connect(m_blameTimer, &QTimer::timeout, this, [this] {
        if (m_blameOn)
            emit blameRequested();
    });
    connect(document(), &QTextDocument::contentsChanged, this, [this] {
        if (m_blameOn)
            m_blameTimer->start(); // re-blame once typing pauses
    });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& rect, int dy) {
        if (!m_blameOn)
            return;
        if (dy)
            m_blameGutter->scroll(0, dy);
        else
            m_blameGutter->update(0, rect.y(), m_blameGutter->width(), rect.height());
    });
}

void Editor::showBlame()
{
    if (m_blameOn)
        return;
    m_blameOn = true;
    setViewportMargins(blameGutterWidth(), 0, 0, 0);
    m_blameGutter->setGeometry(QRect(contentsRect().left(), contentsRect().top(), blameGutterWidth(), contentsRect().height()));
    m_blameGutter->show();
    emit blameShownChanged(true);
    emit blameRequested();
}

void Editor::hideBlame()
{
    if (!m_blameOn)
        return;
    m_blameOn = false;
    m_blameTimer->stop();
    m_blame.clear();
    m_blameGutter->hide();
    setViewportMargins(0, 0, 0, 0);
    emit blameShownChanged(false);
}

void Editor::setBlame(const QList<BlameLine>& lines)
{
    if (!m_blameOn)
        return; // closed while it was being computed
    m_blame = lines;
    m_blameGutter->update();
}

int Editor::blameGutterWidth() const
{
    return fontMetrics().horizontalAdvance(QStringLiteral("0000000 0000-00-00")) + 16;
}

QString Editor::blameLabel(int blockNumber, bool always) const
{
    if (blockNumber < 0 || blockNumber >= m_blame.size())
        return {};
    const BlameLine& line = m_blame[blockNumber];
    if (!always && blockNumber > 0 && m_blame[blockNumber - 1].id == line.id)
        return {}; // within a run from one commit
    if (line.id.isEmpty())
        return tr("Not committed");
    return line.shortId + QLatin1Char(' ') + line.time.toString(QStringLiteral("yyyy-MM-dd"));
}

QString Editor::blameToolTip(int blockNumber) const
{
    if (blockNumber < 0 || blockNumber >= m_blame.size())
        return {};
    const BlameLine& line = m_blame[blockNumber];
    if (line.id.isEmpty())
        return tr("Not committed yet");
    return QStringLiteral("%1 %2\n%3 · %4").arg(line.shortId, line.summary, line.author,
                                               line.time.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
}

void Editor::paintBlame(QPaintEvent* event)
{
    QPainter painter(m_blameGutter);
    painter.fillRect(event->rect(), palette().alternateBase());
    painter.setFont(font());
    QTextBlock block = firstVisibleBlock();
    const int firstVisible = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    while (block.isValid() && top <= event->rect().bottom()) {
        const int height = qRound(blockBoundingRect(block).height());
        if (block.isVisible() && top + height >= event->rect().top()) {
            // A run that starts above the view is labelled again at its top.
            const QString label = block.blockNumber() == firstVisible ? blameLabel(block.blockNumber(), true)
                                                                       : blameLabel(block.blockNumber());
            if (!label.isEmpty()) {
                const bool committed = block.blockNumber() < m_blame.size() && !m_blame[block.blockNumber()].id.isEmpty();
                QFont font = painter.font();
                font.setItalic(!committed);
                painter.setFont(font);
                painter.setPen(palette().color(committed ? QPalette::Text : QPalette::PlaceholderText));
                // A block that starts above the view (a long wrapped paragraph) is labelled at the top edge.
                const int y = std::max(top, 0);
                painter.drawText(QRect(6, y, m_blameGutter->width() - 10, fontMetrics().height()), Qt::AlignLeft, label);
            }
        }
        block = block.next();
        top += height;
    }
}

void Editor::resizeEvent(QResizeEvent* event)
{
    QPlainTextEdit::resizeEvent(event);
    const QRect area = contentsRect();
    m_blameGutter->setGeometry(QRect(area.left(), area.top(), blameGutterWidth(), area.height()));
}

void Editor::setCatalogue(const NoteCatalogue* catalogue)
{
    delete m_completer;
    m_completer = catalogue ? new WikilinkCompleter(this, catalogue, m_path) : nullptr;
    disconnect(m_catalogueReloaded);
    if (catalogue) {
        m_catalogueReloaded = connect(catalogue, &NoteCatalogue::reloaded, this,
                                      [this, catalogue] { setProblems(catalogue->problemsFor(m_path)); });
        setProblems(catalogue->problemsFor(m_path));
    }
}

void Editor::setProblems(const QList<ProblemRef>& problems)
{
    m_problems = problems;
    applyProblems();
}

QString Editor::ensureBlockId(bool* added)
{
    if (added)
        *added = false;
    const QByteArray text = toPlainText().toUtf8();
    const auto block = Transclusion::blockAt(text, textCursor().blockNumber() + 1);
    if (!block)
        return {};
    if (!block->id.isEmpty())
        return block->id;
    // Six letters and digits, not already an ID in this note.
    QSet<QString> taken;
    for (const Transclusion::Block& named : Transclusion::blocks(text))
        taken.insert(named.id);
    static const QString kAlphabet = QStringLiteral("abcdefghijklmnopqrstuvwxyz0123456789");
    QString id;
    do {
        id.clear();
        for (int i = 0; i < 6; ++i)
            id += kAlphabet.at(QRandomGenerator::global()->bounded(int(kAlphabet.size())));
    } while (taken.contains(id));
    const QTextBlock last = document()->findBlockByNumber(block->lastLine - 1);
    QString line = last.text();
    qsizetype keep = line.size();
    while (keep > 0 && line.at(keep - 1).isSpace())
        --keep;
    QTextCursor cursor(last);
    cursor.beginEditBlock();
    cursor.setPosition(last.position() + int(keep));
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    cursor.insertText(QStringLiteral(" ^") + id);
    cursor.endEditBlock();
    if (added)
        *added = true;
    return id;
}

void Editor::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu* menu = createStandardContextMenu(event->pos());
    if (!m_contextActions.isEmpty()) {
        menu->addSeparator();
        menu->addActions(m_contextActions);
    }
    menu->exec(event->globalPos());
    delete menu;
}

void Editor::applyProblems()
{
    QList<QTextEdit::ExtraSelection> selections;
    for (const ProblemRef& problem : std::as_const(m_problems)) {
        const QTextBlock block = document()->findBlockByNumber(problem.line - 1);
        if (!block.isValid())
            continue;
        const QString text = block.text();
        qsizetype start = 0;
        qsizetype length = text.size();
        if (!problem.subject.isEmpty()) {
            start = text.indexOf(problem.subject);
            if (start < 0)
                continue; // the line has changed since the index read it
            length = problem.subject.size();
        }
        if (length == 0)
            continue;
        QTextEdit::ExtraSelection selection;
        selection.cursor = QTextCursor(block);
        selection.cursor.setPosition(block.position() + int(start));
        selection.cursor.setPosition(block.position() + int(start + length), QTextCursor::KeepAnchor);
        selection.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        selection.format.setUnderlineColor(Theme::instance().colour(problem.level == QLatin1String("error")     ? Theme::Colour::Error
                                                                    : problem.level == QLatin1String("warning") ? Theme::Colour::Warning
                                                                                                                : Theme::Colour::Info));
        selection.format.setToolTip(problem.message);
        selections.append(selection);
    }
    setExtraSelections(selections);
}

QString Editor::problemAt(int position) const
{
    QStringList messages;
    for (const QTextEdit::ExtraSelection& selection : extraSelections()) {
        if (!selection.format.toolTip().isEmpty() && position >= selection.cursor.selectionStart()
            && position < selection.cursor.selectionEnd())
            messages.append(selection.format.toolTip());
    }
    return messages.join(QLatin1Char('\n'));
}

bool Editor::viewportEvent(QEvent* event)
{
    if (event->type() == QEvent::ToolTip) {
        const auto* help = static_cast<QHelpEvent*>(event);
        const QString text = problemAt(cursorForPosition(help->pos()).position());
        if (text.isEmpty())
            QToolTip::hideText();
        else
            QToolTip::showText(help->globalPos(), text, viewport());
        return true;
    }
    return QPlainTextEdit::viewportEvent(event);
}

void Editor::keyPressEvent(QKeyEvent* event)
{
    if (m_completer && m_completer->handleKeyPress(event))
        return;
    QPlainTextEdit::keyPressEvent(event);
    // Only typing opens the popup; reloads and programmatic edits do not.
    if (m_completer && !event->text().isEmpty())
        m_completer->refresh();
    else if (m_completer && (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right))
        m_completer->refresh();
}

bool Editor::isModified() const
{
    return document()->isModified();
}

std::expected<QByteArray, QString> Editor::diskBytes() const
{
    return m_root.readBytes(m_path);
}

QByteArray Editor::currentHashOnDisk() const
{
    if (!QFileInfo::exists(m_path))
        return {};
    const auto bytes = diskBytes();
    return bytes ? ContentHash::of(*bytes) : QByteArray();
}

void Editor::setBuffer(const QString& text)
{
    // Keep the view where it was across a reload.
    const int position = textCursor().position();
    const int scroll = verticalScrollBar()->value();
    setPlainText(text);
    applyProblems(); // the old selections went with the old text
    QTextCursor cursor = textCursor();
    cursor.setPosition(std::min(position, int(document()->characterCount()) - 1));
    setTextCursor(cursor);
    verticalScrollBar()->setValue(scroll);
}

void Editor::setConflict(bool conflict)
{
    if (m_conflict == conflict)
        return;
    m_conflict = conflict;
    emit conflictChanged(conflict);
}

std::expected<void, QString> Editor::load()
{
    const auto bytes = diskBytes();
    if (!bytes)
        return std::unexpected(bytes.error());
    setBuffer(QString::fromUtf8(*bytes));
    m_baseHash = ContentHash::of(*bytes);
    document()->setModified(false);
    setConflict(false);
    return {};
}

void Editor::restore(const QString& text, const QByteArray& baseHash)
{
    setBuffer(text);
    m_baseHash = baseHash;
    document()->setModified(true);
}

Editor::SaveResult Editor::save(QString* error)
{
    // AV-002: never overwrite a version this buffer was not based on.
    const QByteArray onDisk = currentHashOnDisk();
    if (!onDisk.isEmpty() && onDisk != m_baseHash) {
        setConflict(true);
        return SaveResult::Conflict;
    }

    const QByteArray content = toPlainText().toUtf8();
    if (const auto written = m_root.writeNote(m_path, content); !written) {
        if (error)
            *error = written.error();
        return SaveResult::Failed;
    }
    m_baseHash = ContentHash::of(content);
    document()->setModified(false);
    setConflict(false);
    return SaveResult::Saved;
}

Editor::DiskState Editor::checkDisk()
{
    if (!QFileInfo::exists(m_path)) {
        // Keep the text: saving recreates the note.
        if (!m_baseHash.isEmpty()) {
            m_baseHash.clear();
            document()->setModified(true);
        }
        return DiskState::Deleted;
    }
    const QByteArray onDisk = currentHashOnDisk();
    if (onDisk.isEmpty() || onDisk == m_baseHash)
        return DiskState::Unchanged; // includes the echo of our own save
    if (!isModified()) {
        if (!load())
            return DiskState::Unchanged;
        return DiskState::Reloaded;
    }
    setConflict(true);
    return DiskState::Conflict;
}

void Editor::keepMine()
{
    m_baseHash = currentHashOnDisk();
    document()->setModified(true);
    setConflict(false);
}

std::expected<void, QString> Editor::takeDisk()
{
    return load();
}

} // namespace coeus
