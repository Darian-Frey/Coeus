#pragma once

#include "index/Search.hpp"

#include <QWidget>

class QLabel;
class QLineEdit;
class QTimer;
class QTreeWidget;

namespace coeus {

// The search pane (F-006): a query box over the index with results as you
// type. Each search runs off the UI thread after a short pause in typing; a
// newer search supersedes an older one still running.
class SearchPanel : public QWidget {
    Q_OBJECT

public:
    explicit SearchPanel(QWidget* parent = nullptr);

    void setIndexPath(const QString& indexPath);
    // Runs the current query again, e.g. after the index changed.
    void refresh();
    void setQuery(const QString& text);

    QLineEdit* input() const { return m_input; }
    QTreeWidget* results() const { return m_results; }
    QLabel* status() const { return m_status; }

    static constexpr int kTypingDelayMs = 150;

signals:
    // A hit was chosen: open it and select the first of `words`.
    void openRequested(const QString& absolutePath, const QStringList& words);
    void finished(int hits);

private:
    void run();
    void present(const std::expected<QList<Search::Hit>, QString>& result, qint64 elapsedMs);

    QString m_indexPath;
    int m_generation = 0;
    QStringList m_words; // of the query on screen
    QLineEdit* m_input = nullptr;
    QLabel* m_status = nullptr;
    QTreeWidget* m_results = nullptr;
    QTimer* m_typingTimer = nullptr;
};

} // namespace coeus
