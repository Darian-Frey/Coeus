#pragma once

#include "index/Backlinks.hpp"

#include <QWidget>

class QLabel;
class QToolButton;
class QTreeWidget;

namespace coeus {

// Sidebar section for F-005: notes linking to the current note, with the
// linking line, and below them unlinked mentions of its title and aliases.
// Results are collected off the UI thread; a newer request supersedes an
// older one still running.
class BacklinksPanel : public QWidget {
    Q_OBJECT

public:
    explicit BacklinksPanel(QWidget* parent = nullptr);

    void setIndexPath(const QString& indexPath);
    // The note whose backlinks to show; empty for none.
    void showNote(const QString& notePath);
    void refresh();

    QTreeWidget* linksTree() const { return m_links; }
    QTreeWidget* mentionsTree() const { return m_mentions; }

signals:
    void openRequested(const QString& absolutePath, int line);
    // Results for the current note are on screen (for tests and status).
    void updated(int links, int mentions);

private:
    void present(const Backlinks::Result& result);
    static void fill(QTreeWidget* tree, const QList<Backlinks::Reference>& references);

    QString m_indexPath;
    QString m_notePath;
    int m_generation = 0;
    QLabel* m_linksLabel = nullptr;
    QTreeWidget* m_links = nullptr;
    QToolButton* m_mentionsHeader = nullptr;
    QTreeWidget* m_mentions = nullptr;
};

} // namespace coeus
