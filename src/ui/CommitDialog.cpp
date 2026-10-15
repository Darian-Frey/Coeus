#include "ui/CommitDialog.hpp"

#include "ui/Theme.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace coeus {

namespace {

constexpr const char* kPushSetting = "git/pushAfterCommit";

} // namespace

CommitDialog::CommitDialog(QWidget* parent)
    : QDialog(parent)
    , m_heading(new QLabel(this))
    , m_tree(new QTreeWidget(this))
    , m_unsaved(new QLabel(this))
    , m_saveAll(new QPushButton(tr("Save All"), this))
    , m_message(new QPlainTextEdit(this))
    , m_push(new QCheckBox(tr("Push after committing"), this))
    , m_error(new QLabel(this))
    , m_commit(new QPushButton(tr("Commit"), this))
    , m_cancel(new QPushButton(tr("Cancel"), this))
{
    setObjectName(QStringLiteral("commitDialog"));
    setWindowTitle(tr("Commit"));
    resize(560, 560);

    m_heading->setTextFormat(Qt::PlainText);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({tr("Change"), tr("Path")});
    m_tree->setRootIsDecorated(false);
    m_tree->header()->setStretchLastSection(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    connect(m_tree, &QTreeWidget::itemChanged, this, &CommitDialog::updateCommitButton);

    m_unsaved->setWordWrap(true);
    m_unsaved->setForegroundRole(QPalette::PlaceholderText);
    connect(m_saveAll, &QPushButton::clicked, this, [this] { emit saveAllRequested(m_workdir); });

    Theme::setMonospace(m_message);
    m_message->setPlaceholderText(tr("Summary in one line\n\nThen why, in as many paragraphs as it takes."));
    m_message->setTabChangesFocus(true);
    connect(m_message, &QPlainTextEdit::textChanged, this, &CommitDialog::updateCommitButton);

    m_push->setChecked(QSettings().value(QLatin1String(kPushSetting), false).toBool());
    connect(m_push, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue(QLatin1String(kPushSetting), on); });

    m_error->setWordWrap(true);
    m_error->setTextFormat(Qt::PlainText);
    m_error->setStyleSheet(Theme::instance().errorStyle());
    m_error->hide();

    m_commit->setDefault(true);
    connect(m_commit, &QPushButton::clicked, this, [this] {
        m_error->hide();
        emit commitRequested(m_workdir, selectedPaths(), message(), m_push->isChecked());
    });
    connect(m_cancel, &QPushButton::clicked, this, &QDialog::reject);

    auto* unsaved = new QHBoxLayout;
    unsaved->addWidget(m_unsaved, 1);
    unsaved->addWidget(m_saveAll);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_push, 1);
    buttons->addWidget(m_cancel);
    buttons->addWidget(m_commit);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_heading);
    layout->addWidget(m_tree, 2);
    layout->addLayout(unsaved);
    layout->addWidget(new QLabel(tr("Message"), this));
    layout->addWidget(m_message, 1);
    layout->addWidget(m_error);
    layout->addLayout(buttons);
    updateCommitButton();
}

void CommitDialog::setRepository(const QString& workdir, const QString& branch, const QList<GitChange>& changes,
                                 const QStringList& unsaved)
{
    m_workdir = workdir;
    m_heading->setText(tr("%1 on %2 · %n change(s)", nullptr, int(changes.size())).arg(QFileInfo(workdir).fileName(), branch));
    m_heading->setToolTip(workdir);
    {
        const QSignalBlocker blocker(m_tree);
        // Keep the author's ticks across a refresh (after Save All, say).
        QHash<QString, Qt::CheckState> ticks;
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
            ticks.insert(m_tree->topLevelItem(i)->text(1), m_tree->topLevelItem(i)->checkState(0));
        m_tree->clear();
        for (const GitChange& change : changes) {
            auto* item = new QTreeWidgetItem(m_tree, {change.letter(), change.path});
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            const bool note = change.path.endsWith(QLatin1String(".md"), Qt::CaseInsensitive);
            const bool ticked = change.kind != GitChange::Kind::Untracked || note;
            item->setCheckState(0, ticks.value(change.path, ticked ? Qt::Checked : Qt::Unchecked));
            item->setToolTip(1, change.staged ? tr("Staged") : QString());
        }
    }
    m_unsaved->setText(unsaved.isEmpty() ? QString()
                                         : tr("Unsaved, so not in this commit: %1").arg(unsaved.join(QStringLiteral(", "))));
    m_unsaved->setVisible(!unsaved.isEmpty());
    m_saveAll->setVisible(!unsaved.isEmpty());
    m_error->hide();
    updateCommitButton();
}

void CommitDialog::setBusy(bool busy, const QString& what)
{
    m_tree->setEnabled(!busy);
    m_message->setReadOnly(busy);
    m_push->setEnabled(!busy);
    m_saveAll->setEnabled(!busy);
    m_commit->setText(busy ? what : tr("Commit"));
    updateCommitButton();
    if (busy)
        m_commit->setEnabled(false);
}

void CommitDialog::showError(const QString& message)
{
    m_error->setText(message);
    m_error->show();
}

QStringList CommitDialog::selectedPaths() const
{
    QStringList paths;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        if (m_tree->topLevelItem(i)->checkState(0) == Qt::Checked)
            paths.append(m_tree->topLevelItem(i)->text(1));
    }
    return paths;
}

QString CommitDialog::message() const
{
    return m_message->toPlainText();
}

void CommitDialog::updateCommitButton()
{
    m_commit->setEnabled(!selectedPaths().isEmpty() && !message().trimmed().isEmpty() && !m_message->isReadOnly());
}

} // namespace coeus
