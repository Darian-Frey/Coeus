#include "ui/OutlinePanel.hpp"

#include "parser/NoteParser.hpp"
#include "ui/Editor.hpp"
#include "ui/NoteView.hpp"

#include <QFutureWatcher>
#include <QLabel>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace coeus {

namespace {

constexpr int kIndexRole = Qt::UserRole + 1; // position in m_headings

} // namespace

OutlinePanel::OutlinePanel(QWidget* parent)
    : QWidget(parent)
    , m_empty(new QLabel(this))
    , m_tree(new QTreeWidget(this))
    , m_parseTimer(new QTimer(this))
{
    m_empty->setForegroundRole(QPalette::PlaceholderText);
    m_empty->setWordWrap(true);

    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setIndentation(14);
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    m_tree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tree->setTextElideMode(Qt::ElideRight);
    connect(m_tree, &QTreeWidget::itemClicked, this, &OutlinePanel::activate);
    connect(m_tree, &QTreeWidget::itemActivated, this, &OutlinePanel::activate);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_empty);
    layout->addWidget(m_tree);

    m_parseTimer->setSingleShot(true);
    m_parseTimer->setInterval(kParseDelayMs);
    connect(m_parseTimer, &QTimer::timeout, this, &OutlinePanel::parse);
    present({});
}

void OutlinePanel::setView(NoteView* view)
{
    if (view == m_view && view)
        return;
    for (const QMetaObject::Connection& c : std::as_const(m_connections))
        disconnect(c);
    m_connections.clear();
    m_view = view;
    ++m_generation;
    m_parseTimer->stop();
    if (!view) {
        present({});
        return;
    }
    Editor* editor = view->editor();
    m_connections.append(connect(editor, &QPlainTextEdit::textChanged, m_parseTimer, qOverload<>(&QTimer::start)));
    m_connections.append(connect(editor, &QPlainTextEdit::cursorPositionChanged, this, &OutlinePanel::followCursor));
    parse();
}

void OutlinePanel::parse()
{
    if (!m_view) {
        present({});
        return;
    }
    const int generation = ++m_generation;
    auto* watcher = new QFutureWatcher<QList<Heading>>(this);
    connect(watcher, &QFutureWatcher<QList<Heading>>::finished, this, [this, watcher, generation] {
        watcher->deleteLater();
        if (generation == m_generation)
            present(watcher->result());
    });
    watcher->setFuture(QtConcurrent::run([text = m_view->editor()->toPlainText().toUtf8()] {
        return NoteParser::parse(text).headings;
    }));
}

void OutlinePanel::present(const QList<Heading>& headings)
{
    m_headings = headings;
    m_items.clear();
    m_tree->clear();

    // Nest by level: each heading goes under the nearest shallower one.
    std::vector<std::pair<int, QTreeWidgetItem*>> stack;
    for (qsizetype i = 0; i < headings.size(); ++i) {
        const Heading& heading = headings[i];
        while (!stack.empty() && stack.back().first >= heading.level)
            stack.pop_back();
        auto* item = stack.empty() ? new QTreeWidgetItem(m_tree) : new QTreeWidgetItem(stack.back().second);
        item->setText(0, heading.text);
        item->setToolTip(0, QStringLiteral("H%1 · line %2").arg(heading.level).arg(heading.line));
        item->setData(0, kIndexRole, int(i));
        stack.emplace_back(heading.level, item);
        m_items.append(item);
    }
    m_tree->expandAll();
    m_tree->setVisible(!headings.isEmpty());
    m_empty->setText(m_view ? tr("No headings in this note.") : tr("Open a note to see its outline."));
    m_empty->setVisible(headings.isEmpty());
    followCursor();
    emit updated(int(headings.size()));
}

void OutlinePanel::followCursor()
{
    if (!m_view || m_headings.isEmpty())
        return;
    // The section holding the cursor: the last heading at or above its line.
    const int line = m_view->editor()->textCursor().blockNumber() + 1;
    QTreeWidgetItem* current = nullptr;
    for (qsizetype i = 0; i < m_headings.size() && m_headings[i].line <= line; ++i)
        current = m_items[i];
    const QSignalBlocker blocker(m_tree);
    m_tree->setCurrentItem(current);
    if (!current)
        m_tree->clearSelection();
}

void OutlinePanel::activate(QTreeWidgetItem* item)
{
    if (!m_view || !item)
        return;
    const int index = item->data(0, kIndexRole).toInt();
    if (index < 0 || index >= m_headings.size())
        return;
    // Which occurrence of this text it is, for headings that repeat.
    int occurrence = 0;
    for (int i = 0; i < index; ++i)
        occurrence += m_headings[i].text == m_headings[index].text ? 1 : 0;
    m_view->goToHeading(m_headings[index].line, m_headings[index].text, occurrence, index);
}

} // namespace coeus
