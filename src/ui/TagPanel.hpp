#pragma once

#include "index/Tags.hpp"

#include <QWidget>

class QLabel;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace coeus {

// The tag pane (F-007): every tag with the number of notes carrying it or a
// tag beneath it, nested on '/'. Loaded off the UI thread whenever the index
// changes. Activating a tag asks for the explorer to show its notes.
class TagPanel : public QWidget {
    Q_OBJECT

public:
    explicit TagPanel(QWidget* parent = nullptr);

    void setIndexPath(const QString& indexPath);
    void reload();

    // Notes tagged `tag` or beneath it, from the last load.
    QList<Tags::Usage> notesFor(const QString& tag) const;

    QTreeWidget* tree() const { return m_tree; }
    QLineEdit* filter() const { return m_filter; }

signals:
    void tagActivated(const QString& tag);
    void reloaded(int tags);

private:
    void present();
    void addNodes(QTreeWidgetItem* parent, const QList<Tags::Node>& nodes);

    QString m_indexPath;
    QList<Tags::Usage> m_usages;
    int m_generation = 0;
    QLineEdit* m_filter = nullptr;
    QLabel* m_empty = nullptr;
    QTreeWidget* m_tree = nullptr;
};

} // namespace coeus
