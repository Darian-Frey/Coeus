#pragma once

#include "index/IndexQueries.hpp"

#include <QWidget>

#include <optional>

class QComboBox;
class QToolButton;
class QLabel;
class QTreeWidget;

namespace coeus {

class NoteCatalogue;

// The Problems panel (F-016): the register lint's findings from the
// catalogue's snapshot, grouped by file, for the current note's project or
// every project, filtered by level. Activating a problem asks for its file
// at its line. Never queries on the UI thread.
class ProblemsPanel : public QWidget {
    Q_OBJECT

public:
    enum class Scope { Project, All };

    explicit ProblemsPanel(const NoteCatalogue* catalogue, QWidget* parent = nullptr);

    // Narrows "This project" to the project containing `absolutePath`, if any.
    void followNote(const QString& absolutePath);
    // Re-reads the catalogue (after it reloads).
    void refresh();

    QComboBox* scopeBox() const { return m_scope; }
    QComboBox* levelBox() const { return m_level; }
    QTreeWidget* tree() const { return m_tree; }
    QString summary() const;
    // What the panel shows, as Markdown for an AI session or an issue (F-037).
    QString report() const;
    QToolButton* copyButton() const { return m_copy; }

signals:
    void openRequested(const QString& absolutePath, int line);
    // Copy Report put `problems` problems on the clipboard.
    void reportCopied(int problems);

private:
    void present();
    QList<ProblemRef> shown() const;
    std::optional<ProjectRef> followedProject() const;

    const NoteCatalogue* m_catalogue;
    QComboBox* m_scope = nullptr;
    QComboBox* m_level = nullptr;
    QLabel* m_count = nullptr;
    QTreeWidget* m_tree = nullptr;
    QToolButton* m_copy = nullptr;
    QString m_projectFolder; // absolute folder of the followed project; empty if none
};

} // namespace coeus
