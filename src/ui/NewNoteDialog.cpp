#include "ui/NewNoteDialog.hpp"

#include "vault/NewNote.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace coeus {

NewNoteDialog::NewNoteDialog(const QList<Root>& roots, const QList<Template>& templates, int rootIndex, const QString& folder,
                             QWidget* parent)
    : QDialog(parent)
    , m_roots(roots)
    , m_templates(templates)
{
    setWindowTitle(tr("New Note"));
    m_name = new QLineEdit(this);
    m_name->setPlaceholderText(tr("Note name"));
    m_root = new QComboBox(this);
    for (const Root& root : std::as_const(m_roots))
        m_root->addItem(root.name);
    m_root->setCurrentIndex(std::clamp(rootIndex, 0, int(m_roots.size()) - 1));
    m_folder = new QLineEdit(folder, this);
    m_folder->setPlaceholderText(tr("(top level)"));
    m_template = new QComboBox(this);
    m_template->addItem(tr("Blank"));
    for (const Template& t : std::as_const(m_templates))
        m_template->addItem(t.folder.isEmpty() ? t.name : tr("%1  → %2/").arg(t.name, t.folder));
    m_message = new QLabel(this);
    m_message->setWordWrap(true);
    m_message->setTextFormat(Qt::PlainText);

    auto* form = new QFormLayout;
    form->addRow(tr("&Name:"), m_name);
    if (m_roots.size() > 1)
        form->addRow(tr("&Root:"), m_root);
    else
        m_root->hide();
    form->addRow(tr("&Folder:"), m_folder);
    form->addRow(tr("&Template:"), m_template);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_ok = buttons->button(QDialogButtonBox::Ok);
    m_ok->setText(tr("Create"));
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_message);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_name, &QLineEdit::textChanged, this, &NewNoteDialog::validate);
    connect(m_folder, &QLineEdit::textChanged, this, &NewNoteDialog::validate);
    connect(m_root, &QComboBox::currentIndexChanged, this, &NewNoteDialog::validate);
    connect(m_template, &QComboBox::currentIndexChanged, this, &NewNoteDialog::templateChosen);
    validate();
    m_name->setFocus();
    resize(460, sizeHint().height());
}

QString NewNoteDialog::templatePath() const
{
    const int index = m_template->currentIndex() - 1; // 0 is Blank
    return index >= 0 && index < m_templates.size() ? m_templates[index].path : QString();
}

QString NewNoteDialog::message() const
{
    return m_message->text();
}

void NewNoteDialog::templateChosen()
{
    const int index = m_template->currentIndex() - 1;
    if (index >= 0 && index < m_templates.size() && !m_templates[index].folder.isEmpty()) {
        m_root->setCurrentIndex(0); // template folders are in the vault
        m_folder->setText(m_templates[index].folder);
    }
    validate();
}

void NewNoteDialog::validate()
{
    m_path.clear();
    const int index = m_root->currentIndex();
    if (index < 0 || index >= m_roots.size()) {
        m_ok->setEnabled(false);
        return;
    }
    const Root& root = m_roots[index];
    if (m_name->text().trimmed().isEmpty()) {
        m_message->setText(tr("Name the note."));
        m_ok->setEnabled(false);
        return;
    }
    const auto path = NewNote::pathFor(root.path, m_folder->text(), m_name->text());
    if (!path) {
        m_message->setText(path.error() + QLatin1Char('.'));
        m_ok->setEnabled(false);
        return;
    }
    m_path = *path;
    const QString relative = QDir(root.path).relativeFilePath(m_path);
    m_message->setText(tr("Creates %1").arg(index == 0 ? relative : root.name + QLatin1Char('/') + relative));
    m_ok->setEnabled(true);
}

} // namespace coeus
