#pragma once

#include "git/GitRepository.hpp"

#include <QWidget>

class QLabel;
class QPlainTextEdit;

namespace coeus {

// A note as it was in a commit (F-020), read-only in its own tab. Not an
// Editor: it has no file to save to, so no save, journal or conflict path
// can reach it.
class VersionView : public QWidget {
    Q_OBJECT

public:
    VersionView(const QString& notePath, const GitCommit& commit, const QString& text, QWidget* parent = nullptr);

    QString notePath() const { return m_notePath; }
    const GitCommit& commit() const { return m_commit; }
    QPlainTextEdit* text() const { return m_text; }
    // "DECISIONS @ a41c9e2"
    QString title() const;

private:
    QString m_notePath;
    GitCommit m_commit;
    QLabel* m_header = nullptr;
    QPlainTextEdit* m_text = nullptr;
};

} // namespace coeus
