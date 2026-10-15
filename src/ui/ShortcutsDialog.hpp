#pragma once

#include <QDialog>
#include <QList>

class QAction;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;

namespace coeus {

// View › Keyboard Shortcuts… (F-027): every registered command with its
// shortcut and default. Choosing a row and pressing keys gives it that
// shortcut at once, unless another command has it, which is said and
// refused; Clear removes it, Reset restores the default, Reset All every one.
class ShortcutsDialog : public QDialog {
    Q_OBJECT

public:
    ShortcutsDialog(const QList<QAction*>& actions, QWidget* parent = nullptr);

    QTreeWidget* tree() const { return m_tree; }
    QLineEdit* filterEdit() const { return m_filter; }
    QKeySequenceEdit* keyEdit() const { return m_keys; }
    QString message() const;
    // As pressing `sequence` in the key field for the selected command; false if refused.
    bool assign(const QKeySequence& sequence);

private:
    QAction* selected() const;
    void fill();
    void selectionChanged();
    void filter(const QString& text);

    QList<QAction*> m_actions;
    QLineEdit* m_filter = nullptr;
    QTreeWidget* m_tree = nullptr;
    QKeySequenceEdit* m_keys = nullptr;
    QPushButton* m_clear = nullptr;
    QPushButton* m_reset = nullptr;
    QLabel* m_message = nullptr;
};

} // namespace coeus
