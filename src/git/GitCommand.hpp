#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;
class QTimer;

namespace coeus {

// Runs one `git` command that talks to a remote, `git push` (F-022) or
// `git fetch` (F-032), as D-020 decides, so the author's credential helper,
// SSH agent and hooks apply exactly as in a terminal. Asynchronous: the
// process runs alongside the UI thread and reports through finished(). Git
// may not prompt: with no usable credential it fails rather than waiting
// for input that never comes.
class GitCommand : public QObject {
    Q_OBJECT

public:
    static constexpr int kTimeoutMs = 120'000;

    explicit GitCommand(QObject* parent = nullptr);
    ~GitCommand() override;

    // Starts `git -C <workdir> <arguments>`; false if one is already running.
    bool start(const QString& workdir, const QStringList& arguments);
    bool isRunning() const;

signals:
    // `message` is git's own telling line: its error on failure, its last line on success.
    void finished(bool ok, const QString& message);

private:
    QProcess* m_process = nullptr;
    QTimer* m_timeout = nullptr;
};

} // namespace coeus
