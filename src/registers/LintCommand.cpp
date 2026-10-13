#include "registers/LintCommand.hpp"

#include "index/Database.hpp"
#include "index/IndexQueries.hpp"
#include "index/IndexStore.hpp"
#include "index/Indexer.hpp"
#include "parser/RegisterParser.hpp"
#include "registers/Lint.hpp"
#include "vault/GitIgnore.hpp"
#include "vault/Vault.hpp"
#include "vault/VaultConfig.hpp"

#include <QFile>
#include <QSet>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>
#include <tuple>

namespace coeus {

namespace {

constexpr int kFailed = 2;

// IDs mentioned in a project's notes that no entry of that project defines.
// A citation already reported as L1 is not reported again.
std::expected<QList<ProblemRef>, QString> undefinedMentions(const QString& indexPath, const QString& rootPath,
                                                            const QList<ProblemRef>& reported)
{
    auto db = Database::open(indexPath, Database::Mode::ReadOnly);
    if (!db)
        return std::unexpected(db.error());
    QSet<QPair<std::int64_t, QString>> defined;
    auto entries = db->prepare("SELECT project_id, ident FROM registers WHERE project_id IS NOT NULL");
    if (!entries)
        return std::unexpected(entries.error());
    while (entries->step().value_or(false))
        defined.insert({entries->int64(0), entries->text(1)});
    // L1s already reported, as "path\x1fline\x1fident".
    const auto key = [](const QString& path, int line, const QString& ident) {
        return path + QLatin1Char('\x1f') + QString::number(line) + QLatin1Char('\x1f') + ident;
    };
    QSet<QString> unresolved;
    for (const ProblemRef& problem : reported) {
        if (problem.rule == QLatin1String("L1"))
            unresolved.insert(key(problem.notePath, problem.line, problem.subject));
    }

    QList<ProblemRef> found;
    auto notes = db->prepare("SELECT path, project_id FROM notes WHERE project_id IS NOT NULL ORDER BY path");
    if (!notes)
        return std::unexpected(notes.error());
    while (notes->step().value_or(false)) {
        const QString path = rootPath + QLatin1Char('/') + notes->text(0);
        const std::int64_t project = notes->int64(1);
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return std::unexpected(QStringLiteral("Cannot read %1").arg(path));
        for (const auto& [ident, line] : RegisterParser::mentionsIn(QString::fromUtf8(file.readAll()))) {
            if (defined.contains({project, ident}) || unresolved.contains(key(path, line, ident)))
                continue;
            found.append({QStringLiteral("vault"), rootPath, path, QStringLiteral("mention"), QStringLiteral("error"), line,
                          QStringLiteral("%1 is mentioned but no entry in this project defines it").arg(ident), ident});
        }
    }
    return found;
}

} // namespace

int LintCommand::run(const QString& folder, const Options& options, QTextStream& out, QTextStream& err)
{
    const auto vault = Vault::open(folder);
    if (!vault) {
        err << "coeus --lint: " << vault.error() << '\n';
        return kFailed;
    }
    const QString rootPath = vault->rootPath();

    // A throwaway index: linting never writes beside the vault.
    QTemporaryDir scratch;
    if (!scratch.isValid()) {
        err << "coeus --lint: cannot create a temporary index\n";
        return kFailed;
    }
    const QString indexPath = scratch.filePath(QStringLiteral("index.db"));
    {
        auto store = IndexStore::open(indexPath);
        if (!store) {
            err << "coeus --lint: " << store.error() << '\n';
            return kFailed;
        }
        const RootInfo root{0, QStringLiteral("vault"), rootPath};
        if (auto put = store->putRoot(root); !put) {
            err << "coeus --lint: " << put.error() << '\n';
            return kFailed;
        }
        GitIgnore ignore(rootPath);
        Indexer indexer(*store);
        const auto scanned = indexer.scan(root, [&ignore](const QString& path, bool isDir) { return ignore.isIgnored(path, isDir); });
        if (!scanned) {
            err << "coeus --lint: " << scanned.error() << '\n';
            return kFailed;
        }
    }

    const auto projects = IndexQueries::projects(indexPath);
    if (!projects) {
        err << "coeus --lint: " << projects.error() << '\n';
        return kFailed;
    }
    if (projects->isEmpty()) {
        err << "coeus --lint: no project in " << rootPath
            << " (a project is a folder whose README.md opens with the status header)\n";
        return 1;
    }

    const int threshold = int(VaultConfig::integer(rootPath, "registers", "review_threshold_days")
                                  .value_or(Lint::kDefaultReviewThresholdDays));
    auto problems = IndexQueries::problems(indexPath, options.today, threshold);
    if (!problems) {
        err << "coeus --lint: " << problems.error() << '\n';
        return kFailed;
    }
    if (options.mentions) {
        const auto extra = undefinedMentions(indexPath, rootPath, *problems);
        if (!extra) {
            err << "coeus --lint: " << extra.error() << '\n';
            return kFailed;
        }
        problems->append(*extra);
        std::stable_sort(problems->begin(), problems->end(), [](const ProblemRef& a, const ProblemRef& b) {
            return std::tie(a.notePath, a.line) < std::tie(b.notePath, b.line);
        });
    }

    int counts[3] = {0, 0, 0}; // error, warning, info
    for (const ProblemRef& problem : std::as_const(*problems)) {
        const int level = problem.level == QLatin1String("error") ? 0 : problem.level == QLatin1String("warning") ? 1 : 2;
        ++counts[level];
        if (options.quiet && level != 0)
            continue;
        out << problem.notePath.mid(rootPath.size() + 1) << ':' << problem.line << ": " << problem.level << ": "
            << problem.message << " (" << problem.rule << ")\n";
    }
    out.flush();
    err << "coeus --lint: " << counts[0] << " error(s), " << counts[1] << " warning(s), " << counts[2] << " info, in "
        << projects->size() << (projects->size() == 1 ? " project\n" : " projects\n");
    err.flush();
    return counts[0] > 0 || (options.strict && counts[1] > 0) ? 1 : 0;
}

} // namespace coeus
