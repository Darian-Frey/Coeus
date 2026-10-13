#include "ui/Editor.hpp"

#include "index/ContentHash.hpp"
#include "ui/MarkdownHighlighter.hpp"
#include "ui/NoteCatalogue.hpp"
#include "ui/WikilinkCompleter.hpp"

#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QScrollBar>
#include <QTextBlock>
#include <QToolTip>

namespace coeus {

Editor::Editor(const Vault& root, const QString& absolutePath, QWidget* parent)
    : QPlainTextEdit(parent)
    , m_root(root)
    , m_path(absolutePath)
    , m_highlighter(new MarkdownHighlighter(document()))
{
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setLineWrapMode(QPlainTextEdit::WidgetWidth);
    setTabChangesFocus(false);
    connect(document(), &QTextDocument::modificationChanged, this, &Editor::modificationChanged);
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
        selection.format.setUnderlineColor(problem.level == QLatin1String("error")     ? QColor(0xd3, 0x2f, 0x2f)
                                           : problem.level == QLatin1String("warning") ? QColor(0xe6, 0x8a, 0x00)
                                                                                       : QColor(0x19, 0x76, 0xd2));
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
