#pragma once

#include "index/Search.hpp"

#include <QWidget>

#include <memory>

class QComboBox;
class QToolButton;
class QLabel;
class QThreadPool;
class QLineEdit;
class QTimer;
class QTreeWidget;

namespace coeus {

// The search pane (F-006): a query box over the index with results as you
// type. Each search runs off the UI thread after a short pause in typing; a
// newer search supersedes an older one still running. Three modes: Words
// (full text), Meaning (nearest passages, F-018) and Both (the two merged by
// rank). Meaning needs the embedding model, which the pane loads once, on a
// thread of its own, the first time it is asked.
class SearchPanel : public QWidget {
    Q_OBJECT

public:
    explicit SearchPanel(QWidget* parent = nullptr);
    ~SearchPanel() override;

    enum class Mode { Words, Meaning, Both };

    void setIndexPath(const QString& indexPath);
    // The embedding model file (Embedder::modelPath for the vault).
    void setModelPath(const QString& modelPath);
    void setMode(Mode mode);
    Mode mode() const;
    QComboBox* modeBox() const { return m_mode; }
    // Runs the current query again, e.g. after the index changed.
    void refresh();
    void setQuery(const QString& text);
    // "words", "meaning" or "both", as bookmarks keep them (F-014).
    static QString modeName(Mode mode);
    static Mode modeNamed(const QString& name);
    // The ☆ beside the query: filled while the query and mode are bookmarked.
    void setStarred(bool starred);
    QToolButton* starButton() const { return m_star; }

    QLineEdit* input() const { return m_input; }
    QTreeWidget* results() const { return m_results; }
    QLabel* status() const { return m_status; }

    static constexpr int kTypingDelayMs = 150;

signals:
    // A hit was chosen: open it and select the first of `words`.
    void openRequested(const QString& absolutePath, const QStringList& words);
    // A passage found by meaning was chosen: open it at its first line.
    void openAtLineRequested(const QString& absolutePath, int line);
    void finished(int hits);
    // The ☆ was clicked with `query` in `mode` ("words", "meaning", "both").
    void starToggled(const QString& query, const QString& mode);
    // The query or the mode changed, so the ☆ may need to change too.
    void queryChanged(const QString& query, const QString& mode);

private:
    struct Row {
        QString notePath;
        QString title;   // bold
        QString detail;  // the snippet, as HTML
        int line = 0;    // > 0: open there rather than at the words
    };
    struct Outcome {
        std::expected<QList<Row>, QString> rows;
        QString note; // e.g. how much of the vault has vectors yet
        qint64 elapsedMs = 0;
    };
    struct Model; // the pane's own embedder, used only on m_modelPool

    void run();
    void present(const Outcome& outcome);

    QString m_indexPath;
    std::shared_ptr<Model> m_model;
    QThreadPool* m_modelPool = nullptr;
    QComboBox* m_mode = nullptr;
    QToolButton* m_star = nullptr;
    int m_generation = 0;
    QStringList m_words; // of the query on screen
    QLineEdit* m_input = nullptr;
    QLabel* m_status = nullptr;
    QTreeWidget* m_results = nullptr;
    QTimer* m_typingTimer = nullptr;
};

} // namespace coeus
