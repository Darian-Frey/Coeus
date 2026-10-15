#include "ui/ShortcutsDialog.hpp"

#include "ui/Shortcuts.hpp"

#include <QAction>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace coeus {

namespace {

enum Column { Command, Shortcut, Default };

QString commandName(const QAction* action)
{
    QString text = action->text();
    text.remove(QLatin1Char('&'));
    return text;
}

QString shown(const QKeySequence& sequence)
{
    return sequence.toString(QKeySequence::NativeText);
}

} // namespace

ShortcutsDialog::ShortcutsDialog(const QList<QAction*>& actions, QWidget* parent)
    : QDialog(parent)
    , m_actions(actions)
    , m_filter(new QLineEdit(this))
    , m_tree(new QTreeWidget(this))
    , m_keys(new QKeySequenceEdit(this))
    , m_clear(new QPushButton(tr("Clear"), this))
    , m_reset(new QPushButton(tr("Reset"), this))
    , m_message(new QLabel(this))
{
    setWindowTitle(tr("Keyboard Shortcuts"));
    std::sort(m_actions.begin(), m_actions.end(),
              [](const QAction* a, const QAction* b) { return commandName(a).localeAwareCompare(commandName(b)) < 0; });
    m_filter->setPlaceholderText(tr("Filter commands or keys"));
    m_filter->setClearButtonEnabled(true);
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({tr("Command"), tr("Shortcut"), tr("Default")});
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->header()->setSectionResizeMode(Command, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(Shortcut, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(Default, QHeaderView::ResizeToContents);
    m_tree->header()->setStretchLastSection(false);
    m_message->setWordWrap(true);
    m_message->setTextFormat(Qt::PlainText);

    auto* editRow = new QHBoxLayout;
    editRow->addWidget(new QLabel(tr("Press keys:"), this));
    editRow->addWidget(m_keys, 1);
    editRow->addWidget(m_clear);
    editRow->addWidget(m_reset);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    QPushButton* resetAll = buttons->addButton(tr("Reset All"), QDialogButtonBox::ResetRole);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_filter);
    layout->addWidget(m_tree, 1);
    layout->addLayout(editRow);
    layout->addWidget(m_message);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_filter, &QLineEdit::textChanged, this, &ShortcutsDialog::filter);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, &ShortcutsDialog::selectionChanged);
    connect(m_keys, &QKeySequenceEdit::editingFinished, this, [this] { assign(m_keys->keySequence()); });
    connect(m_clear, &QPushButton::clicked, this, [this] { assign(QKeySequence()); });
    connect(m_reset, &QPushButton::clicked, this, [this] {
        if (QAction* action = selected())
            assign(Shortcuts::defaultOf(action));
    });
    connect(resetAll, &QPushButton::clicked, this, [this] {
        for (QAction* action : std::as_const(m_actions))
            Shortcuts::set(action, QKeySequence()); // clear first, so defaults never collide midway
        for (QAction* action : std::as_const(m_actions))
            Shortcuts::reset(action);
        fill();
        m_message->setText(tr("Every shortcut is back to its default."));
    });
    fill();
    m_tree->setCurrentItem(m_tree->topLevelItem(0));
    resize(560, 520);
}

QString ShortcutsDialog::message() const
{
    return m_message->text();
}

QAction* ShortcutsDialog::selected() const
{
    QTreeWidgetItem* item = m_tree->currentItem();
    return item ? m_actions.value(item->data(Command, Qt::UserRole).toInt()) : nullptr;
}

void ShortcutsDialog::fill()
{
    const int current = m_tree->currentItem() ? m_tree->currentItem()->data(Command, Qt::UserRole).toInt() : 0;
    m_tree->clear();
    for (qsizetype i = 0; i < m_actions.size(); ++i) {
        const QAction* action = m_actions[i];
        auto* item = new QTreeWidgetItem(m_tree, {commandName(action), shown(action->shortcut()), shown(Shortcuts::defaultOf(action))});
        item->setData(Command, Qt::UserRole, int(i));
        item->setForeground(Default, palette().brush(QPalette::PlaceholderText));
        if (action->shortcut() != Shortcuts::defaultOf(action)) {
            QFont font = item->font(Shortcut);
            font.setBold(true); // changed from the default
            item->setFont(Shortcut, font);
        }
        if (i == current)
            m_tree->setCurrentItem(item);
    }
    filter(m_filter->text());
}

void ShortcutsDialog::selectionChanged()
{
    QAction* action = selected();
    m_keys->setEnabled(action);
    m_clear->setEnabled(action && !action->shortcut().isEmpty());
    m_reset->setEnabled(action && action->shortcut() != Shortcuts::defaultOf(action));
    const QSignalBlocker block(m_keys);
    m_keys->setKeySequence(action ? action->shortcut() : QKeySequence());
}

void ShortcutsDialog::filter(const QString& text)
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_tree->topLevelItem(i);
        item->setHidden(!text.isEmpty() && !item->text(Command).contains(text, Qt::CaseInsensitive)
                        && !item->text(Shortcut).contains(text, Qt::CaseInsensitive));
    }
}

bool ShortcutsDialog::assign(const QKeySequence& sequence)
{
    QAction* action = selected();
    if (!action)
        return false;
    if (QAction* other = Shortcuts::conflict(m_actions, action, sequence)) {
        m_message->setText(tr("%1 is already the shortcut for %2. Change or clear that one first.")
                               .arg(shown(sequence), commandName(other)));
        selectionChanged(); // the field shows the command's own shortcut again
        return false;
    }
    Shortcuts::set(action, sequence);
    m_message->setText(sequence.isEmpty() ? tr("%1 has no shortcut.").arg(commandName(action))
                                          : tr("%1 is now %2.").arg(commandName(action), shown(sequence)));
    fill();
    selectionChanged();
    return true;
}

} // namespace coeus
