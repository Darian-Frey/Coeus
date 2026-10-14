#pragma once

#include "index/IndexQueries.hpp"

#include <QDialog>

class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;

namespace coeus {

// Promote idea (F-032): the codename and where the project goes, in the
// vault's projects/ or a new folder that becomes a linked root (F-034).
// It only asks; MainWindow does the work when it is accepted.
class PromoteDialog : public QDialog {
    Q_OBJECT

public:
    PromoteDialog(const IdeaRef& idea, const QString& vaultPath, QWidget* parent = nullptr);

    const IdeaRef& idea() const { return m_idea; }
    QString codename() const;
    bool toLinkedRoot() const;
    // The project's folder, absolute; empty while the choices are incomplete.
    QString targetFolder() const;
    // The linked root's name, when toLinkedRoot().
    QString rootName() const;

    QLineEdit* codenameEdit() const { return m_codename; }
    QRadioButton* vaultChoice() const { return m_inVault; }
    QRadioButton* linkedChoice() const { return m_linked; }
    QLineEdit* parentEdit() const { return m_parent; }
    QPushButton* promoteButton() const;

private:
    void update();

    IdeaRef m_idea;
    QString m_vaultPath;
    QLineEdit* m_codename = nullptr;
    QRadioButton* m_inVault = nullptr;
    QRadioButton* m_linked = nullptr;
    QLineEdit* m_parent = nullptr;
    QPushButton* m_browse = nullptr;
    QLabel* m_preview = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
};

} // namespace coeus
