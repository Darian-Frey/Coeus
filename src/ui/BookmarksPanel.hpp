#pragma once

#include <QList>
#include <QString>
#include <QWidget>

class QLabel;
class QTreeWidget;

namespace coeus {

// The Bookmarks pane (F-014): starred notes and searches in the order they
// were starred. Activating one opens the note or runs the search; the
// context menu (or Delete) removes one and moves it up or down. It shows what
// MainWindow gives it and only reports what was asked for.
class BookmarksPanel : public QWidget {
    Q_OBJECT

public:
    struct Row {
        QString label;   // a note's title, or the search's text
        QString detail;  // a note's display path, or the search's mode
        bool search = false;
        bool missing = false; // a note no longer there
    };

    explicit BookmarksPanel(QWidget* parent = nullptr);
    void setRows(const QList<Row>& rows);
    QTreeWidget* tree() const { return m_tree; }

signals:
    void activated(int index);
    void removeRequested(int index);
    void moveRequested(int index, int to);

private:
    void showMenu(const QPoint& at);

    QTreeWidget* m_tree = nullptr;
    QLabel* m_empty = nullptr;
};

} // namespace coeus
