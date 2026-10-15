#pragma once

#include "git/GitRepository.hpp"

#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QToolButton;

namespace coeus {

// Two versions of a note compared inline (F-021), read-only in its own tab:
// the whole note in reading order, each removed line (red, "−") above the
// line that replaced it (green, "+"), with old and new line numbers. Like
// VersionView it is not an editor, so nothing can save it.
class DiffView : public QWidget {
    Q_OBJECT

public:
    DiffView(const QString& notePath, const QString& oldLabel, const QString& newLabel, const QList<DiffLine>& lines,
             QWidget* parent = nullptr);

    QString notePath() const { return m_notePath; }
    QString title() const;
    QPlainTextEdit* text() const { return m_text; }
    QString summary() const;
    int added() const { return m_added; }
    int removed() const { return m_removed; }
    // Moves to the next (or previous) run of changed lines; false if there is none.
    bool nextChange(bool forward = true);

private:
    QString m_notePath;
    QString m_oldLabel;
    QString m_newLabel;
    void paintChanges(); // added and removed lines, in the theme's colours
    QList<int> m_changeStarts; // block numbers where a run of changes begins
    QList<DiffLine::Kind> m_kinds; // per block
    int m_added = 0;
    int m_removed = 0;
    QLabel* m_header = nullptr;
    QToolButton* m_previous = nullptr;
    QToolButton* m_next = nullptr;
    QPlainTextEdit* m_text = nullptr;
};

} // namespace coeus
