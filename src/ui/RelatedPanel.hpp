#pragma once

#include "search/VectorStore.hpp"

#include <QWidget>

class QLabel;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace coeus {

// Sidebar section for F-019: the five notes nearest in meaning to the
// current note, leaving out those it already links to, each with the
// passage that matched and a Link here button. Read from stored vectors on a
// background thread (no model is needed); a newer request supersedes an
// older one still running.
class RelatedPanel : public QWidget {
    Q_OBJECT

public:
    explicit RelatedPanel(QWidget* parent = nullptr);

    void setIndexPath(const QString& indexPath);
    // The note to find relations for; empty for none.
    void showNote(const QString& notePath);
    void refresh();

    QString notePath() const { return m_notePath; }
    QTreeWidget* tree() const { return m_tree; }
    QString status() const;
    // The Link here button of row `row` (for tests).
    QToolButton* linkButton(int row) const;

signals:
    void openRequested(const QString& absolutePath, int line);
    // Put a link to `absolutePath` at the cursor of the current note.
    void linkRequested(const QString& absolutePath);
    // Results for the current note are on screen.
    void updated(int notes);

private:
    void present(const std::expected<SemanticSearch::Related, QString>& result);

    QString m_indexPath;
    QString m_notePath;
    int m_generation = 0;
    QLabel* m_status = nullptr;
    QTreeWidget* m_tree = nullptr;
};

} // namespace coeus
