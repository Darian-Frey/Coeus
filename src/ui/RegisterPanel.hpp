#pragma once

#include "index/IndexQueries.hpp"

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QTreeWidget;

namespace coeus {

class NoteCatalogue;

// The register panel (F-015): one project's entries, filtered by kind,
// status and text, sorted by kind then ID. Follows the current note's
// project; reads the catalogue's snapshot, so it never queries on the UI
// thread. Activating an entry asks for its register file at its line.
class RegisterPanel : public QWidget {
    Q_OBJECT

public:
    explicit RegisterPanel(const NoteCatalogue* catalogue, QWidget* parent = nullptr);

    // Shows the project containing `absolutePath`, if there is one.
    void followNote(const QString& absolutePath);
    // Re-reads the catalogue (after it reloads).
    void refresh();

    QComboBox* projectBox() const { return m_project; }
    QComboBox* kindBox() const { return m_kind; }
    QComboBox* statusBox() const { return m_status; }
    QLineEdit* filter() const { return m_filter; }
    QTreeWidget* tree() const { return m_tree; }

signals:
    void openRequested(const QString& absolutePath, int line);

private:
    void present();
    QList<RegisterRef> entriesOfProject() const;

    const NoteCatalogue* m_catalogue;
    QComboBox* m_project = nullptr;
    QComboBox* m_kind = nullptr;
    QComboBox* m_status = nullptr;
    QLineEdit* m_filter = nullptr;
    QLabel* m_count = nullptr;
    QTreeWidget* m_tree = nullptr;
    bool m_updating = false;
};

} // namespace coeus
