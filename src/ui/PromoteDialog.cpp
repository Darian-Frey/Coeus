#include "ui/PromoteDialog.hpp"

#include "registers/ProjectScaffold.hpp"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace coeus {

PromoteDialog::PromoteDialog(const IdeaRef& idea, const QString& vaultPath, QWidget* parent)
    : QDialog(parent)
    , m_idea(idea)
    , m_vaultPath(vaultPath)
    , m_codename(new QLineEdit(this))
    , m_inVault(new QRadioButton(this))
    , m_linked(new QRadioButton(tr("In a new folder outside the vault, as a linked root, under:"), this))
    , m_parent(new QLineEdit(this))
    , m_browse(new QPushButton(tr("Browse…"), this))
    , m_preview(new QLabel(this))
    , m_buttons(new QDialogButtonBox(QDialogButtonBox::Cancel, this))
{
    setObjectName(QStringLiteral("promoteDialog"));
    setWindowTitle(tr("Promote Idea"));
    m_codename->setText(idea.codename.isEmpty() ? idea.title : idea.codename);
    m_inVault->setChecked(true);
    m_parent->setText(QDir::homePath() + QStringLiteral("/Projects"));
    m_preview->setWordWrap(true);
    m_preview->setTextFormat(Qt::PlainText);
    m_preview->setForegroundRole(QPalette::PlaceholderText);
    m_buttons->addButton(tr("Promote"), QDialogButtonBox::AcceptRole);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_browse, &QPushButton::clicked, this, [this] {
        const QString folder = QFileDialog::getExistingDirectory(this, tr("Folder for the new project"), m_parent->text());
        if (!folder.isEmpty())
            m_parent->setText(folder);
    });
    for (QLineEdit* edit : {m_codename, m_parent})
        connect(edit, &QLineEdit::textChanged, this, &PromoteDialog::update);
    connect(m_linked, &QRadioButton::toggled, this, &PromoteDialog::update);

    auto* where = new QHBoxLayout;
    where->addWidget(m_parent, 1);
    where->addWidget(m_browse);
    auto* form = new QFormLayout;
    form->addRow(tr("Codename"), m_codename);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Promote %1 to a project: its note becomes the README, with the status header and "
                                    "the standard's skeletons beside it, and the idea note is removed.")
                                     .arg(QFileInfo(idea.notePath).fileName()),
                                 this));
    layout->addLayout(form);
    layout->addWidget(m_inVault);
    layout->addWidget(m_linked);
    layout->addLayout(where);
    layout->addWidget(m_preview);
    layout->addWidget(m_buttons);
    update();
}

QString PromoteDialog::codename() const
{
    return m_codename->text().trimmed();
}

bool PromoteDialog::toLinkedRoot() const
{
    return m_linked->isChecked();
}

QString PromoteDialog::targetFolder() const
{
    const QString folder = ProjectScaffold::folderName(codename());
    if (folder.isEmpty())
        return {};
    if (!toLinkedRoot())
        return m_vaultPath + QStringLiteral("/projects/") + folder;
    const QString parent = m_parent->text().trimmed();
    if (parent.isEmpty() || !QDir::isAbsolutePath(QDir::fromNativeSeparators(QDir::cleanPath(parent))))
        return {};
    return QDir::cleanPath(parent) + QLatin1Char('/') + folder;
}

QString PromoteDialog::rootName() const
{
    return ProjectScaffold::rootName(codename());
}

QPushButton* PromoteDialog::promoteButton() const
{
    for (QAbstractButton* button : m_buttons->buttons()) {
        if (m_buttons->buttonRole(button) == QDialogButtonBox::AcceptRole)
            return qobject_cast<QPushButton*>(button);
    }
    return nullptr;
}

void PromoteDialog::update()
{
    const QString folder = ProjectScaffold::folderName(codename());
    m_inVault->setText(tr("In the vault: projects/%1/").arg(folder.isEmpty() ? tr("<codename>") : folder));
    m_parent->setEnabled(toLinkedRoot());
    m_browse->setEnabled(toLinkedRoot());
    const QString target = targetFolder();
    QString problem;
    if (folder.isEmpty())
        problem = tr("A codename is a folder name: letters, digits, spaces, '.', '_' or '-', not starting with '.'.");
    else if (target.isEmpty())
        problem = tr("Choose the folder to create it in.");
    else if (QFileInfo::exists(target) && !QDir(target).isEmpty())
        problem = tr("%1 already exists and is not empty.").arg(target);
    else if (toLinkedRoot() && (target == m_vaultPath || target.startsWith(m_vaultPath + QLatin1Char('/'))))
        problem = tr("A linked root lies outside the vault; choose a folder outside %1.").arg(m_vaultPath);
    else if (toLinkedRoot() && rootName().isEmpty())
        problem = tr("The codename leaves no letters for the root's name.");
    m_preview->setText(problem.isEmpty() ? (toLinkedRoot() ? tr("Creates %1 and links it as the root \"%2\"; the vault reopens to show it.")
                                                                 .arg(target, rootName())
                                                           : tr("Creates %1.").arg(target))
                                         : problem);
    if (QPushButton* promote = promoteButton())
        promote->setEnabled(problem.isEmpty());
}

} // namespace coeus
