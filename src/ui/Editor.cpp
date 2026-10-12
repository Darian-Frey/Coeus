#include "ui/Editor.hpp"

#include "index/ContentHash.hpp"
#include "ui/MarkdownHighlighter.hpp"

#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QScrollBar>
#include <QTextBlock>

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
