#pragma once

#include <QDialog>
#include <QList>
#include <QString>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace coeus {

// File › New Note… (F-036): a name, where it goes and a template. A template
// whose frontmatter names a folder (`coeus-folder`) moves the note there when
// chosen. OK stays disabled while the note could not be created, with the
// reason shown; it never overwrites (NewNote::pathFor).
class NewNoteDialog : public QDialog {
    Q_OBJECT

public:
    struct Root {
        QString name; // as the explorer shows it
        QString path; // absolute
    };
    struct Template {
        QString name;   // relative to the templates folder, without .md
        QString path;   // absolute
        QString folder; // its coeus-folder, relative to the vault; empty for none
    };

    // `roots` starts with the vault. The note starts in `folder` of
    // roots[rootIndex], the template as Blank.
    NewNoteDialog(const QList<Root>& roots, const QList<Template>& templates, int rootIndex, const QString& folder,
                  QWidget* parent = nullptr);

    QString notePath() const { return m_path; } // absolute; empty while invalid
    QString templatePath() const;               // empty for Blank

    QLineEdit* nameEdit() const { return m_name; }
    QLineEdit* folderEdit() const { return m_folder; }
    QComboBox* rootBox() const { return m_root; }
    QComboBox* templateBox() const { return m_template; }
    QPushButton* okButton() const { return m_ok; }
    QString message() const;

private:
    void templateChosen();
    void validate();

    QList<Root> m_roots;
    QList<Template> m_templates;
    QLineEdit* m_name = nullptr;
    QComboBox* m_root = nullptr;
    QLineEdit* m_folder = nullptr;
    QComboBox* m_template = nullptr;
    QLabel* m_message = nullptr;
    QPushButton* m_ok = nullptr;
    QString m_path;
};

} // namespace coeus
