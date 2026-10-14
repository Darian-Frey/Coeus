#pragma once

#include <QObject>
#include <QString>

class QProcess;
class QTimer;

namespace coeus {

// Pushes a repository's current branch by running `git push` (D-020), so the
// author's credential helper, SSH agent and hooks apply exactly as in a
// terminal. Asynchronous: the process runs alongside the UI thread and
// reports through finished(). Git may not prompt: with no usable
// credential it fails rather than waiting for input that never comes.
class GitPush : public QObject {
    Q_OBJECT

public:
    static constexpr int kTimeoutMs = 120'000;

    explicit GitPush(QObject* parent = nullptr);
    ~GitPush() override;

    // Starts `git -C <workdir> push`; false if a push is already running.
    bool start(const QString& workdir);
    bool isRunning() const;

signals:
    // `message` is git's own last line on failure, or a summary on success.
    void finished(bool ok, const QString& message);

private:
    QProcess* m_process = nullptr;
    QTimer* m_timeout = nullptr;
};

} // namespace coeus
