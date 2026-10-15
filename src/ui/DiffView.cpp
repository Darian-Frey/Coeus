#include "ui/DiffView.hpp"

#include "ui/Theme.hpp"

#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QToolButton>
#include <QVBoxLayout>

namespace coeus {

namespace {

QString number(int n)
{
    return n > 0 ? QString::number(n).rightJustified(4) : QStringLiteral("    ");
}

} // namespace

DiffView::DiffView(const QString& notePath, const QString& oldLabel, const QString& newLabel, const QList<DiffLine>& lines,
                   QWidget* parent)
    : QWidget(parent)
    , m_notePath(notePath)
    , m_oldLabel(oldLabel)
    , m_newLabel(newLabel)
    , m_header(new QLabel(this))
    , m_previous(new QToolButton(this))
    , m_next(new QToolButton(this))
    , m_text(new QPlainTextEdit(this))
{
    m_text->setReadOnly(true);
    Theme::setMonospace(m_text);
    m_text->setLineWrapMode(QPlainTextEdit::NoWrap);

    // One block per diff line: "  12  13   text", " 12      − text", "      13 + text".
    QStringList text;
    QList<DiffLine::Kind> kinds;
    for (const DiffLine& line : lines) {
        const QChar mark = line.kind == DiffLine::Kind::Added ? QChar(u'+') : line.kind == DiffLine::Kind::Removed ? QChar(u'−') : QChar(u' ');
        text.append(QStringLiteral("%1 %2 %3 %4").arg(number(line.oldLine), number(line.newLine)).arg(mark).arg(line.text));
        kinds.append(line.kind);
        m_added += line.kind == DiffLine::Kind::Added;
        m_removed += line.kind == DiffLine::Kind::Removed;
    }
    m_text->setPlainText(text.join(QLatin1Char('\n')));

    for (qsizetype i = 0; i < kinds.size(); ++i) {
        if (kinds[i] != DiffLine::Kind::Context && (i == 0 || kinds[i - 1] == DiffLine::Kind::Context))
            m_changeStarts.append(int(i));
    }
    m_kinds = kinds;
    paintChanges();
    connect(&Theme::instance(), &Theme::changed, this, &DiffView::paintChanges);

    m_header->setText(summary());
    m_header->setTextFormat(Qt::PlainText);
    m_header->setWordWrap(true);
    m_header->setForegroundRole(QPalette::PlaceholderText);
    m_previous->setText(QStringLiteral("↑"));
    m_previous->setToolTip(tr("Previous change"));
    m_previous->setAutoRaise(true);
    m_next->setText(QStringLiteral("↓"));
    m_next->setToolTip(tr("Next change"));
    m_next->setAutoRaise(true);
    m_previous->setEnabled(!m_changeStarts.isEmpty());
    m_next->setEnabled(!m_changeStarts.isEmpty());
    connect(m_previous, &QToolButton::clicked, this, [this] { nextChange(false); });
    connect(m_next, &QToolButton::clicked, this, [this] { nextChange(true); });

    auto* header = new QHBoxLayout;
    header->setContentsMargins(6, 4, 6, 4);
    header->addWidget(m_header, 1);
    header->addWidget(m_previous);
    header->addWidget(m_next);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(header);
    layout->addWidget(m_text, 1);
    if (!m_changeStarts.isEmpty())
        QMetaObject::invokeMethod(this, [this] { nextChange(true); }, Qt::QueuedConnection); // open on the first change
}

QString DiffView::title() const
{
    return QStringLiteral("%1: %2 ↔ %3").arg(QFileInfo(m_notePath).completeBaseName(), m_oldLabel, m_newLabel);
}

QString DiffView::summary() const
{
    if (m_added == 0 && m_removed == 0)
        return tr("%1: %2 and %3 are the same").arg(QFileInfo(m_notePath).fileName(), m_oldLabel, m_newLabel);
    return tr("%1: %2 → %3 · +%4 −%5 lines · %6 change(s)")
        .arg(QFileInfo(m_notePath).fileName(), m_oldLabel, m_newLabel)
        .arg(m_added)
        .arg(m_removed)
        .arg(m_changeStarts.size());
}

bool DiffView::nextChange(bool forward)
{
    if (m_changeStarts.isEmpty())
        return false;
    const int here = m_text->textCursor().blockNumber();
    int target = forward ? m_changeStarts.front() : m_changeStarts.back(); // wrap around
    if (forward) {
        for (const int start : m_changeStarts) {
            if (start > here) {
                target = start;
                break;
            }
        }
    } else {
        for (auto it = m_changeStarts.crbegin(); it != m_changeStarts.crend(); ++it) {
            if (*it < here) {
                target = *it;
                break;
            }
        }
    }
    QTextCursor cursor(m_text->document()->findBlockByNumber(target));
    m_text->setTextCursor(cursor);
    m_text->centerCursor();
    return true;
}

void DiffView::paintChanges()
{
    QList<QTextEdit::ExtraSelection> selections;
    const QColor added = Theme::instance().colour(Theme::Colour::Added);
    const QColor removed = Theme::instance().colour(Theme::Colour::Removed);
    for (qsizetype i = 0; i < m_kinds.size(); ++i) {
        if (m_kinds[i] == DiffLine::Kind::Context)
            continue;
        QTextEdit::ExtraSelection selection;
        selection.cursor = QTextCursor(m_text->document()->findBlockByNumber(int(i)));
        selection.format.setBackground(m_kinds[i] == DiffLine::Kind::Added ? added : removed);
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selections.append(selection);
    }
    m_text->setExtraSelections(selections);
}

} // namespace coeus
