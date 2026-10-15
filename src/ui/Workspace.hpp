#pragma once

#include <QList>
#include <QPointer>
#include <QString>
#include <QWidget>

class QSplitter;
class QTabWidget;

namespace coeus {

// The window's central area (F-012, D-023, SPEC.md §Panes): tab areas
// ("panes") side by side or one above another in splitters. It answers the
// calls MainWindow made of its single QTabWidget, with one index running
// across every pane in order, so a tab is found, made current or closed
// wherever it is; new tabs go to the current pane, the one last focused or
// clicked. A pane emptied by closing or moving its last tab goes, unless it
// is the only one.
class Workspace : public QWidget {
    Q_OBJECT

public:
    explicit Workspace(QWidget* parent = nullptr);

    // As QTabWidget, across every pane.
    int count() const;
    QWidget* widget(int index) const;
    int indexOf(QWidget* page) const;
    QWidget* currentWidget() const;
    int currentIndex() const;
    void setCurrentIndex(int index);
    void setCurrentWidget(QWidget* page);
    int addTab(QWidget* page, const QString& title);            // to the current pane
    int insertTab(int index, QWidget* page, const QString& title); // where `index` is
    void removeTab(int index);
    QString tabText(int index) const;
    void setTabText(int index, const QString& text);
    QString tabToolTip(int index) const;
    void setTabToolTip(int index, const QString& tip);

    QList<QTabWidget*> panes() const { return m_panes; }
    QTabWidget* currentPane() const { return m_current; }
    QTabWidget* paneOf(QWidget* page) const;

    // A new pane beside (Qt::Horizontal) or below (Qt::Vertical) the current
    // one; the current tab moves into it if its pane has another tab left.
    void split(Qt::Orientation orientation);
    // The current tab into the next pane; false if there is only one pane.
    bool moveToNextPane();
    // Makes the next pane current, and its tab focused.
    void focusNextPane();

signals:
    void currentChanged(int index);
    void tabCloseRequested(int index);

private:
    QTabWidget* newPane();
    void makeCurrent(QTabWidget* pane);
    void removeIfEmpty(QTabWidget* pane);
    int globalIndex(const QTabWidget* pane, int local) const;
    QTabWidget* paneAt(int index, int* local) const;

    QList<QTabWidget*> m_panes; // in order: as split, left to right, top to bottom
    QPointer<QTabWidget> m_current;
};

} // namespace coeus
