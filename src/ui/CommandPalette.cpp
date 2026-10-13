#include "ui/CommandPalette.hpp"

#include "ui/FuzzyMatch.hpp"

#include <QAction>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

namespace coeus {

namespace {

constexpr int kWidth = 560;
constexpr int kHeight = 360;
constexpr int kTopOffset = 80;

QString plainText(const QAction* action)
{
    QString text = action->text();
    text.remove(QLatin1Char('&'));
    return text;
}

} // namespace

CommandPalette::CommandPalette(QWidget* parent)
    : QDialog(parent, Qt::Popup | Qt::FramelessWindowHint)
    , m_input(new QLineEdit(this))
    , m_list(new QTreeWidget(this))
{
    m_input->setPlaceholderText(tr("Type a command…"));
    m_input->setClearButtonEnabled(true);
    m_input->installEventFilter(this);

    m_list->setColumnCount(2);
    m_list->setHeaderHidden(true);
    m_list->setRootIsDecorated(false);
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->header()->setStretchLastSection(false);
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addWidget(m_input);
    layout->addWidget(m_list);

    resize(kWidth, kHeight);

    connect(m_input, &QLineEdit::textChanged, this, &CommandPalette::refilter);
    connect(m_input, &QLineEdit::returnPressed, this, &CommandPalette::triggerCurrent);
    connect(m_list, &QTreeWidget::itemActivated, this, &CommandPalette::triggerCurrent);
}

void CommandPalette::setActions(const QList<QAction*>& actions)
{
    m_actions.clear();
    for (QAction* action : actions)
        m_actions.append(action);
    refilter();
}

void CommandPalette::popup()
{
    m_input->clear();
    refilter();
    if (QWidget* window = parentWidget() ? parentWidget()->window() : nullptr) {
        const QRect frame = window->geometry();
        move(frame.x() + (frame.width() - width()) / 2, frame.y() + kTopOffset);
    }
    show();
    raise();
    activateWindow();
    m_input->setFocus();
}

int CommandPalette::visibleCount() const
{
    return m_list->topLevelItemCount();
}

void CommandPalette::setFilterText(const QString& text)
{
    m_input->setText(text);
}

void CommandPalette::setPlaceholder(const QString& text)
{
    m_input->setPlaceholderText(text);
}

bool CommandPalette::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        const int rows = m_list->topLevelItemCount();
        if (rows > 0 && (key->key() == Qt::Key_Down || key->key() == Qt::Key_Up)) {
            const int current = m_list->indexOfTopLevelItem(m_list->currentItem());
            const int step = key->key() == Qt::Key_Down ? 1 : -1;
            const int next = std::clamp(current + step, 0, rows - 1);
            m_list->setCurrentItem(m_list->topLevelItem(next));
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void CommandPalette::refilter()
{
    struct Candidate {
        QAction* action;
        QString text;
        int score;
    };

    const QString pattern = m_input->text().trimmed();
    std::vector<Candidate> candidates;
    for (const QPointer<QAction>& action : m_actions) {
        if (!action || !action->isEnabled())
            continue;
        const QString text = plainText(action);
        if (const auto score = fuzzyScore(pattern, text))
            candidates.push_back({action, text, *score});
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.text.localeAwareCompare(b.text) < 0;
    });

    m_list->clear();
    for (const Candidate& candidate : candidates) {
        auto* item = new QTreeWidgetItem(m_list);
        item->setText(0, candidate.text);
        item->setText(1, candidate.action->shortcut().toString(QKeySequence::NativeText));
        item->setForeground(1, palette().brush(QPalette::PlaceholderText));
        item->setData(0, Qt::UserRole, QVariant::fromValue(static_cast<QObject*>(candidate.action)));
    }
    if (m_list->topLevelItemCount() > 0)
        m_list->setCurrentItem(m_list->topLevelItem(0));
}

void CommandPalette::triggerCurrent()
{
    QTreeWidgetItem* item = m_list->currentItem();
    if (!item)
        return;
    QPointer<QAction> action = qobject_cast<QAction*>(item->data(0, Qt::UserRole).value<QObject*>());
    hide();
    if (action)
        action->trigger();
}

} // namespace coeus
