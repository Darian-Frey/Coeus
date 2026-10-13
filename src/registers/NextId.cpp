#include "registers/NextId.hpp"

#include "index/Database.hpp"
#include "parser/RegisterParser.hpp"
#include "registers/RegisterTemplates.hpp"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

namespace coeus {

int NextId::highest(const QString& text, const QString& kind)
{
    int best = 0;
    for (const RegisterEntry& entry : RegisterParser::parse(text)) {
        if (entry.kind == kind)
            best = std::max(best, entry.number);
    }
    return best;
}

std::expected<NextId::Allocation, QString> NextId::allocate(const Request& request)
{
    const QString fileName = RegisterTemplates::fileName(request.kind);
    if (fileName.isEmpty())
        return std::unexpected(QStringLiteral("Not a register kind: %1").arg(request.kind));
    if (!QFileInfo::exists(request.indexPath))
        return std::unexpected(QStringLiteral("The index is not built yet"));
    auto db = Database::open(request.indexPath);
    if (!db)
        return std::unexpected(db.error());

    // BEGIN IMMEDIATE takes the write lock first: no other writer, GUI or
    // coeus --mcp, can allocate between our read and our reservation.
    auto transaction = Transaction::begin(*db, Transaction::Kind::Immediate);
    if (!transaction)
        return std::unexpected(transaction.error());

    auto project = db->prepare("SELECT p.id FROM projects p JOIN roots r ON r.id = p.root_id WHERE r.path = ? AND p.path = ?");
    if (!project)
        return std::unexpected(project.error());
    project->bind(1, request.rootPath).bind(2, request.projectPath.isNull() ? QStringLiteral("") : request.projectPath);
    if (!project->step().value_or(false))
        return std::unexpected(QStringLiteral("Not a project: %1").arg(request.projectPath));
    const std::int64_t projectId = project->int64(0);

    const std::int64_t now = QDateTime::currentSecsSinceEpoch();
    auto expire = db->prepare("DELETE FROM reservations WHERE expires < ?");
    if (!expire || !expire->bind(1, now).run())
        return std::unexpected(QStringLiteral("Cannot clear expired reservations"));

    // The project's register files of this kind, read from disk.
    const QString folder = request.projectPath.isEmpty() ? request.rootPath : request.rootPath + QLatin1Char('/') + request.projectPath;
    Allocation allocation;
    allocation.registerFile = folder + QLatin1Char('/') + fileName;
    int best = highest(request.unsavedText, request.kind);
    auto files = db->prepare("SELECT r.path || '/' || n.path FROM notes n JOIN roots r ON r.id = n.root_id "
                             "WHERE n.project_id = ? AND (n.path = ? OR n.path LIKE ? ESCAPE '\\') ORDER BY length(n.path), n.path");
    if (!files)
        return std::unexpected(files.error());
    const QString relative = request.projectPath.isEmpty() ? fileName : request.projectPath + QLatin1Char('/') + fileName;
    QString escapedName = fileName;
    escapedName.replace(QLatin1Char('_'), QLatin1String("\\_")); // ATTACK_VECTORS.md: '_' is a LIKE wildcard
    files->bind(1, projectId).bind(2, relative).bind(3, QStringLiteral("%/") + escapedName);
    bool first = true;
    while (files->step().value_or(false)) {
        const QString path = files->text(0);
        if (first) { // the shallowest is where new entries go
            allocation.registerFile = path;
            first = false;
        }
        QFile file(path);
        if (file.open(QIODevice::ReadOnly))
            best = std::max(best, highest(QString::fromUtf8(file.readAll()), request.kind));
    }
    allocation.fileExists = QFileInfo::exists(allocation.registerFile);
    if (allocation.fileExists && first) { // on disk but not yet indexed
        QFile file(allocation.registerFile);
        if (file.open(QIODevice::ReadOnly))
            best = std::max(best, highest(QString::fromUtf8(file.readAll()), request.kind));
    }

    auto reserved = db->prepare("SELECT coalesce(max(number), 0) FROM reservations WHERE project_id = ? AND kind = ?");
    if (!reserved)
        return std::unexpected(reserved.error());
    reserved->bind(1, projectId).bind(2, request.kind);
    if (reserved->step().value_or(false))
        best = std::max(best, int(reserved->int64(0)));

    allocation.number = best + 1;
    allocation.ident = QStringLiteral("%1-%2").arg(request.kind).arg(allocation.number, 3, 10, QLatin1Char('0'));
    auto reserve = db->prepare("INSERT INTO reservations(project_id, kind, number, expires, holder) VALUES (?, ?, ?, ?, ?)");
    if (!reserve)
        return std::unexpected(reserve.error());
    if (auto result = reserve->bind(1, projectId)
                          .bind(2, request.kind)
                          .bind(3, allocation.number)
                          .bind(4, now + kReservationSeconds)
                          .bind(5, request.holder)
                          .run();
        !result)
        return std::unexpected(result.error());
    if (auto committed = transaction->commit(); !committed)
        return std::unexpected(committed.error());
    return allocation;
}

} // namespace coeus
