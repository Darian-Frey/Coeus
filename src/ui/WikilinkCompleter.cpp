#include "ui/WikilinkCompleter.hpp"

#include "parser/Transclusion.hpp"
#include "ui/NoteCatalogue.hpp"
#include "ui/WikilinkCompletion.hpp"

#include <QAbstractItemView>
#include <QCompleter>
#include <QFile>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QTextBlock>

namespace coeus {

namespace {

constexpr int kInsertRole = Qt::UserRole + 1;

} // namespace

WikilinkCompleter::WikilinkCompleter(QPlainTextEdit* editor, const NoteCatalogue* catalogue, const QString& notePath)
    : QObject(editor)
    , m_editor(editor)
    , m_catalogue(catalogue)
    , m_notePath(notePath)
    , m_completer(new QCompleter(this))
    , m_model(new QStandardItemModel(this))
{
    m_completer->setWidget(editor);
    m_completer->setModel(m_model);
    m_completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    m_completer->setModelSorting(QCompleter::UnsortedModel);
    m_completer->setMaxVisibleItems(10);
    connect(m_completer, qOverload<const QModelIndex&>(&QCompleter::activated), this, &WikilinkCompleter::insert);
}

QAbstractItemView* WikilinkCompleter::popup() const
{
    return m_completer->popup();
}

bool WikilinkCompleter::isOpen() const
{
    return m_completer->popup()->isVisible();
}

bool WikilinkCompleter::handleKeyPress(QKeyEvent* event)
{
    if (!isOpen())
        return false;
    switch (event->key()) {
    case Qt::Key_Enter:
    case Qt::Key_Return:
    case Qt::Key_Tab:
    case Qt::Key_Escape:
    case Qt::Key_Backtab:
        event->ignore(); // the completer's popup acts on these
        return true;
    default:
        return false;
    }
}

void WikilinkCompleter::close()
{
    m_completer->popup()->hide();
    m_replaceFrom = -1;
}

void WikilinkCompleter::refresh()
{
    const QTextCursor cursor = m_editor->textCursor();
    const QString before = cursor.block().text().left(cursor.positionInBlock());
    static const QRegularExpression kOpenLink(QStringLiteral(R"(\[\[([^\[\]|\n]*)$)"));
    const auto open = kOpenLink.match(before);
    if (cursor.hasSelection()) {
        close();
        return;
    }
    if (!open.hasMatch()) {
        // F-015: "D-" (or "AV-0", …) offers the current project's entries of that kind.
        static const QRegularExpression kIdent(QStringLiteral(R"((?<![\w-])(F|C|D|AV|BUG|IMP)-(\d*)$)"));
        const auto ident = kIdent.match(before);
        const auto project = ident.hasMatch() ? m_catalogue->projectOf(m_notePath) : std::nullopt;
        QList<Completion> completions;
        if (project) {
            const QString kind = ident.captured(1);
            const QString digits = ident.captured(2);
            for (const RegisterRef& entry : m_catalogue->registers()) {
                if (entry.rootPath != project->rootPath || entry.project != project->path || entry.kind != kind)
                    continue;
                if (!digits.isEmpty() && !entry.ident.mid(kind.size() + 1).startsWith(digits)
                    && !QString::number(entry.number).startsWith(digits))
                    continue;
                if (!completions.isEmpty() && completions.back().insert == entry.ident)
                    continue; // a duplicated ident shows once (AV-003 is the lint's business)
                completions.append({entry.ident, entry.status.isEmpty() ? entry.title : entry.title + QStringLiteral(" · ") + entry.status,
                                    entry.ident});
            }
        }
        if (completions.isEmpty()) {
            close();
            return;
        }
        m_replaceFrom = cursor.position() - int(ident.capturedLength());
        m_closesLink = false;
        show(completions);
        return;
    }

    const QString typed = open.captured(1);
    const NoteCatalogue::Location here = m_catalogue->locate(m_notePath);
    QList<Completion> completions;
    const qsizetype hash = typed.indexOf(QLatin1Char('#'));
    if (hash >= 0) {
        // [[Note#… completes that note's headings; [[#… this note's.
        const QString notePart = typed.left(hash);
        QString headingsOf = m_notePath;
        if (!notePart.isEmpty()) {
            const NoteEntry* note = m_catalogue->find(notePart, here.rootName);
            if (!note) {
                close();
                return;
            }
            headingsOf = note->rootPath + QLatin1Char('/') + note->path;
        }
        const QString anchor = typed.mid(hash + 1);
        if (anchor.startsWith(QLatin1Char('^'))) {
            // Block IDs (F-024), read from the text: this note's buffer, unsaved
            // ones included, or the other note's file.
            QByteArray text;
            if (headingsOf == m_notePath) {
                text = m_editor->toPlainText().toUtf8();
            } else {
                QFile file(headingsOf);
                if (file.open(QIODevice::ReadOnly))
                    text = file.readAll();
            }
            QList<QPair<QString, QString>> blocks;
            for (const Transclusion::Block& block : Transclusion::blocks(text))
                blocks.append({block.id, QString::fromUtf8(block.text)});
            completions = WikilinkCompletion::blocks(anchor.mid(1), blocks);
        } else {
            completions = WikilinkCompletion::headings(anchor, m_catalogue->headings(headingsOf));
        }
        m_replaceFrom = cursor.position() - int(typed.size() - hash - 1);
    } else {
        completions = WikilinkCompletion::rank(typed, m_catalogue->notes(), here.rootName, here.folder);
        m_replaceFrom = cursor.position() - int(typed.size());
    }
    if (completions.isEmpty()) {
        close();
        return;
    }
    m_closesLink = true;
    show(completions);
}

void WikilinkCompleter::show(const QList<Completion>& completions)
{
    m_model->clear();
    for (const Completion& completion : std::as_const(completions)) {
        auto* item = new QStandardItem(completion.detail.isEmpty()
                                           ? completion.label
                                           : QStringLiteral("%1    %2").arg(completion.label, completion.detail));
        item->setData(completion.insert, kInsertRole);
        item->setToolTip(completion.insert);
        m_model->appendRow(item);
    }
    QAbstractItemView* view = m_completer->popup();
    view->setCurrentIndex(m_model->index(0, 0));
    // (shared by link and ID completion)
    QRect rect = m_editor->cursorRect();
    rect.setWidth(std::max(320, view->sizeHintForColumn(0) + view->verticalScrollBar()->sizeHint().width()));
    m_completer->complete(rect);
}

void WikilinkCompleter::insert(const QModelIndex& index)
{
    if (m_replaceFrom < 0 || !index.isValid())
        return;
    const QString text = index.data(kInsertRole).toString();
    QTextCursor cursor = m_editor->textCursor();
    cursor.beginEditBlock();
    cursor.setPosition(m_replaceFrom, QTextCursor::KeepAnchor);
    cursor.insertText(text);
    if (m_closesLink) {
        // Close the link unless it already is.
        const QString after = cursor.block().text().mid(cursor.positionInBlock(), 2);
        if (after != QLatin1String("]]"))
            cursor.insertText(QStringLiteral("]]"));
        else
            cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, 2);
    }
    cursor.endEditBlock();
    m_editor->setTextCursor(cursor);
    close();
}

} // namespace coeus
