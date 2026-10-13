#pragma once

#include "parser/ParsedNote.hpp"

#include <QList>
#include <QPointer>
#include <QWidget>

class QLabel;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

namespace coeus {

class NoteView;

// Sidebar section for F-013: the current note's headings as a tree. Built
// from the editor's buffer, so unsaved headings show at once; parsing runs
// off the UI thread after typing pauses. Clicking a heading goes to it in
// whichever mode the tab is in; moving the cursor highlights its section.
class OutlinePanel : public QWidget {
    Q_OBJECT

public:
    explicit OutlinePanel(QWidget* parent = nullptr);

    // The tab to outline; nullptr for none.
    void setView(NoteView* view);

    QTreeWidget* tree() const { return m_tree; }
    const QList<Heading>& headings() const { return m_headings; }

    static constexpr int kParseDelayMs = 300;

signals:
    // A new outline is on screen (for tests).
    void updated(int headings);

private:
    void parse();
    void present(const QList<Heading>& headings);
    void followCursor();
    void activate(QTreeWidgetItem* item);

    QPointer<NoteView> m_view;
    QList<QMetaObject::Connection> m_connections;
    QList<Heading> m_headings;
    QList<QTreeWidgetItem*> m_items; // parallel to m_headings
    int m_generation = 0;
    QLabel* m_empty = nullptr;
    QTreeWidget* m_tree = nullptr;
    QTimer* m_parseTimer = nullptr;
};

} // namespace coeus
