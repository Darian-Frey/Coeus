#include "git/GitCommand.hpp"

#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTimer>

namespace coeus {

namespace {

// git's most telling line: its first "fatal:" or "error:" line if any,
// otherwise its last line that is not a hint.
QString lastLine(const QByteArray& output)
{
    const QStringList lines = QString::fromUtf8(output).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1String("fatal: ")) || trimmed.startsWith(QLatin1String("error: ")))
            return trimmed.section(QLatin1Char(' '), 1);
    }
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        const QString line = it->trimmed();
        if (!line.isEmpty() && !line.startsWith(QLatin1String("hint:")))
            return line;
    }
    return {};
}

} // namespace

GitCommand::GitCommand(QObject* parent)
    : QObject(parent)
    , m_process(new QProcess(this))
    , m_timeout(new QTimer(this))
{
    m_timeout->setSingleShot(true);
    m_timeout->setInterval(kTimeoutMs);
    connect(m_timeout, &QTimer::timeout, this, [this] {
        m_process->kill();
        emit finished(false, tr("git %1 took more than %2 s and was stopped").arg(m_process->arguments().value(2)).arg(kTimeoutMs / 1000));
    });
    connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (!m_timeout->isActive())
            return; // already reported as timed out
        m_timeout->stop();
        const QByteArray output = m_process->readAllStandardError() + m_process->readAllStandardOutput();
        if (status == QProcess::NormalExit && code == 0) {
            emit finished(true, lastLine(output));
        } else {
            const QString reason = lastLine(output);
            emit finished(false, reason.isEmpty() ? tr("git %1 failed (exit %2)").arg(m_process->arguments().value(2)).arg(code) : reason);
        }
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || !m_timeout->isActive())
            return;
        m_timeout->stop();
        emit finished(false, tr("Cannot run git: is it installed?"));
    });
}

GitCommand::~GitCommand()
{
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

bool GitCommand::isRunning() const
{
    return m_process->state() != QProcess::NotRunning;
}

bool GitCommand::start(const QString& workdir, const QStringList& arguments)
{
    if (isRunning())
        return false;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0")); // fail, never wait for a password
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));             // messages we can show as they are
    if (!env.contains(QStringLiteral("GIT_SSH_COMMAND")))
        env.insert(QStringLiteral("GIT_SSH_COMMAND"), QStringLiteral("ssh -o BatchMode=yes"));
    m_process->setProcessEnvironment(env);
    m_process->setProgram(QStringLiteral("git"));
    m_process->setArguments(QStringList{QStringLiteral("-C"), workdir} + arguments);
    m_process->setStandardInputFile(QProcess::nullDevice());
    m_timeout->start();
    m_process->start();
    return true;
}

} // namespace coeus
