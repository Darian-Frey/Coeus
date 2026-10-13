#pragma once

#include <QDialog>
#include <QList>
#include <QPointer>

class QAction;
class QLineEdit;
class QTreeWidget;

namespace coeus {

// Ctrl+P palette over every registered action, filtered by fuzzyScore (F-001).
class CommandPalette : public QDialog {
    Q_OBJECT

public:
    explicit CommandPalette(QWidget* parent = nullptr);

    void setActions(const QList<QAction*>& actions);
    // Clears the filter, shows the palette over the parent window and focuses it.
    void popup();

    // Number of rows currently shown; for tests.
    int visibleCount() const;
    void setFilterText(const QString& text);
    void setPlaceholder(const QString& text);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void refilter();
    void triggerCurrent();

    QLineEdit* m_input = nullptr;
    QTreeWidget* m_list = nullptr;
    QList<QPointer<QAction>> m_actions;
};

} // namespace coeus
