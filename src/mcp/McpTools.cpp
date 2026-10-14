#include "mcp/McpTools.hpp"

#include "index/Backlinks.hpp"
#include "index/Database.hpp"
#include "index/IndexQueries.hpp"
#include "index/Search.hpp"
#include "index/SearchQuery.hpp"
#include "parser/Frontmatter.hpp"
#include "registers/ProjectOverview.hpp"
#include "registers/Vocabulary.hpp"
#include "vault/Vault.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonValue>

#include <algorithm>

namespace coeus {

namespace {

constexpr int kDefaultLimit = 20;
constexpr int kMaxLimit = 100;

QJsonObject schema(const QJsonObject& properties, const QStringList& required = {})
{
    QJsonObject object{{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties},
                       {QStringLiteral("additionalProperties"), false}};
    if (!required.isEmpty())
        object.insert(QStringLiteral("required"), QJsonArray::fromStringList(required));
    return object;
}

QJsonObject stringProperty(const QString& description, const QStringList& values = {})
{
    QJsonObject property{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), description}};
    if (!values.isEmpty())
        property.insert(QStringLiteral("enum"), QJsonArray::fromStringList(values));
    return property;
}

QJsonObject limitProperty()
{
    return {{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1},
            {QStringLiteral("maximum"), kMaxLimit}, {QStringLiteral("description"), QStringLiteral("At most this many results (default 20).")}};
}

QJsonObject tool(const QString& name, const QString& title, const QString& description, const QJsonObject& inputSchema)
{
    return {{QStringLiteral("name"), name}, {QStringLiteral("title"), title}, {QStringLiteral("description"), description},
            {QStringLiteral("inputSchema"), inputSchema}, {QStringLiteral("annotations"), QJsonObject{{QStringLiteral("readOnlyHint"), true}}}};
}

// A string argument: absent is empty; anything but a string is an error.
std::expected<QString, QString> text(const QJsonObject& arguments, const char* key, bool required)
{
    const QJsonValue value = arguments.value(QLatin1String(key));
    if (value.isUndefined() || value.isNull()) {
        if (required)
            return std::unexpected(QStringLiteral("`%1` is required").arg(QLatin1String(key)));
        return QString();
    }
    if (!value.isString())
        return std::unexpected(QStringLiteral("`%1` must be a string").arg(QLatin1String(key)));
    return value.toString().trimmed();
}

std::expected<int, QString> limit(const QJsonObject& arguments)
{
    const QJsonValue value = arguments.value(QStringLiteral("limit"));
    if (value.isUndefined() || value.isNull())
        return kDefaultLimit;
    if (!value.isDouble() || value.toDouble() != double(value.toInt()) || value.toInt() < 1)
        return std::unexpected(QStringLiteral("`limit` must be a positive integer"));
    return std::min(value.toInt(), kMaxLimit);
}

// The root an absolute path is in, and the path relative to it.
QPair<QString, QString> locate(const McpTools::Context& context, const QString& absolutePath)
{
    QPair<QString, QString> best;
    qsizetype length = -1;
    for (const IndexRoots::Root& root : context.roots) {
        const QString prefix = root.info.path + QLatin1Char('/');
        if (absolutePath.startsWith(prefix) && prefix.size() > length) {
            length = prefix.size();
            best = {root.info.name, absolutePath.mid(prefix.size())};
        }
    }
    return best;
}

const IndexRoots::Root* rootNamed(const McpTools::Context& context, const QString& name)
{
    for (const IndexRoots::Root& root : context.roots) {
        if (root.info.name == name)
            return &root;
    }
    return nullptr;
}

QString rootNames(const McpTools::Context& context)
{
    QStringList names;
    for (const IndexRoots::Root& root : context.roots)
        names.append(root.info.name);
    return names.join(QStringLiteral(", "));
}

// A note named by root and root-relative path ('.md' optional), inside that root.
std::expected<QString, QString> notePath(const McpTools::Context& context, const QString& rootName, const QString& path)
{
    const IndexRoots::Root* root = rootNamed(context, rootName.isEmpty() ? QStringLiteral("vault") : rootName);
    if (!root)
        return std::unexpected(QStringLiteral("No root \"%1\"; the roots are: %2").arg(rootName, rootNames(context)));
    QString relative = QDir::cleanPath(path);
    if (relative.isEmpty() || relative == QLatin1String(".") || QDir::isAbsolutePath(relative) || relative.startsWith(QLatin1String("../"))
        || relative == QLatin1String(".."))
        return std::unexpected(QStringLiteral("`path` must be a path inside the root, such as projects/coeus/DECISIONS.md"));
    if (!relative.endsWith(QLatin1String(".md"), Qt::CaseInsensitive))
        relative += QStringLiteral(".md");
    const QString absolute = root->info.path + QLatin1Char('/') + relative;
    const auto vault = Vault::open(root->info.path);
    if (!vault || !QFileInfo(absolute).isFile() || !vault->contains(absolute))
        return std::unexpected(QStringLiteral("No note %1 in %2").arg(relative, root->info.name));
    return absolute;
}

// The project a name means: a board label ("projects/coeus", "pontus",
// "pontus/sub"), case aside, or a folder name only one project has.
std::expected<ProjectRef, QString> findProject(const QList<ProjectRef>& projects, const QString& name)
{
    QList<ProjectRef> byFolder;
    for (const ProjectRef& project : projects) {
        const QString label = ProjectOverview::labelOf(project);
        if (label.compare(name, Qt::CaseInsensitive) == 0)
            return project;
        const QString folder = label.section(QLatin1Char('/'), -1);
        if (folder.compare(name, Qt::CaseInsensitive) == 0)
            byFolder.append(project);
    }
    if (byFolder.size() == 1)
        return byFolder.front();
    QStringList labels;
    for (const ProjectRef& project : projects)
        labels.append(ProjectOverview::labelOf(project));
    return std::unexpected(byFolder.size() > 1
                               ? QStringLiteral("\"%1\" is ambiguous; name one of: %2").arg(name, labels.join(QStringLiteral(", ")))
                               : QStringLiteral("No project \"%1\"; the projects are: %2").arg(name, labels.join(QStringLiteral(", "))));
}

int rootId(const McpTools::Context& context, const QString& name)
{
    const IndexRoots::Root* root = rootNamed(context, name);
    return root ? root->info.id : -1;
}

} // namespace

QJsonArray McpTools::definitions()
{
    return {
        tool(QStringLiteral("search"), QStringLiteral("Search notes"),
             QStringLiteral("Full-text search of every note in the vault and its linked project roots, best first. The query takes "
                            "Coeus's search syntax: words (prefix match), \"exact phrases\", -excluded words, tag:name, path:folder, "
                            "file:name and re:/regex/. Matches in the snippet are wrapped in **."),
             schema({{QStringLiteral("query"), stringProperty(QStringLiteral("What to find."))},
                     {QStringLiteral("mode"), stringProperty(QStringLiteral("\"text\" (default). \"semantic\" is not available until "
                                                                            "Mnemosyne exists."),
                                                             {QStringLiteral("text"), QStringLiteral("semantic")})},
                     {QStringLiteral("limit"), limitProperty()}},
                    {QStringLiteral("query")})),
        tool(QStringLiteral("get_note"), QStringLiteral("Read a note"),
             QStringLiteral("A note's frontmatter, body, outgoing links (with what each resolves to) and backlinks."),
             schema({{QStringLiteral("root"), stringProperty(QStringLiteral("\"vault\" (default) or a linked root's name."))},
                     {QStringLiteral("path"), stringProperty(QStringLiteral("Path within the root, e.g. projects/coeus/DECISIONS.md; .md "
                                                                            "may be left off."))}},
                    {QStringLiteral("path")})),
        tool(QStringLiteral("list_registers"), QStringLiteral("List register entries"),
             QStringLiteral("A project's F-, C-, D-, AV-, BUG- and IMP- entries from its register files, with every field, by kind "
                            "then number."),
             schema({{QStringLiteral("project"), stringProperty(QStringLiteral("The project, as project_status names it (e.g. "
                                                                               "projects/coeus or pontus), or its folder's name if "
                                                                               "only one project has it."))},
                     {QStringLiteral("kind"), stringProperty(QStringLiteral("Only this kind."),
                                                             {QStringLiteral("F"), QStringLiteral("C"), QStringLiteral("D"),
                                                              QStringLiteral("AV"), QStringLiteral("BUG"), QStringLiteral("IMP")})},
                     {QStringLiteral("status"), stringProperty(QStringLiteral("Only entries whose status opens with this, e.g. "
                                                                              "Accepted, open, suggested."))}},
                    {QStringLiteral("project")})),
        tool(QStringLiteral("project_status"), QStringLiteral("Project status"),
             QStringLiteral("Every project (or one) as the project board shows it: README status and Last reviewed, the ROADMAP's "
                            "current phase, unreleased CHANGELOG entries, and Git branch, ahead/behind (as of the last fetch) and "
                            "changed files."),
             schema({{QStringLiteral("project"), stringProperty(QStringLiteral("Only this project."))}})),
        tool(QStringLiteral("list_ideas"), QStringLiteral("List ideas"),
             QStringLiteral("Notes under the vault's ideas/ folder that carry a stage, with their codename, repo and summary."),
             schema({{QStringLiteral("stage"), stringProperty(QStringLiteral("Only this stage: idea, scoped, building, shipped or "
                                                                             "parked."))}})),
    };
}

std::expected<QJsonObject, QString> McpTools::call(const Context& context, const QString& name, const QJsonObject& arguments)
{
    if (name == QLatin1String("search"))
        return search(context, arguments);
    if (name == QLatin1String("get_note"))
        return getNote(context, arguments);
    if (name == QLatin1String("list_registers"))
        return listRegisters(context, arguments);
    if (name == QLatin1String("project_status"))
        return projectStatus(context, arguments);
    if (name == QLatin1String("list_ideas"))
        return listIdeas(context, arguments);
    return std::unexpected(QStringLiteral("No tool \"%1\"").arg(name));
}

std::expected<QJsonObject, QString> McpTools::search(const Context& context, const QJsonObject& arguments)
{
    const auto query = text(arguments, "query", true);
    if (!query)
        return std::unexpected(query.error());
    const auto mode = text(arguments, "mode", false);
    if (!mode)
        return std::unexpected(mode.error());
    if (*mode == QLatin1String("semantic"))
        return std::unexpected(QStringLiteral("Semantic search is not available yet: it needs Mnemosyne (F-018). Use mode \"text\"."));
    if (!mode->isEmpty() && *mode != QLatin1String("text"))
        return std::unexpected(QStringLiteral("`mode` is \"text\" or \"semantic\""));
    const auto count = limit(arguments);
    if (!count)
        return std::unexpected(count.error());
    const auto hits = Search::run(context.indexPath, SearchQuery::parse(*query), *count);
    if (!hits)
        return std::unexpected(hits.error());
    QJsonArray results;
    for (const Search::Hit& hit : *hits) {
        const auto [root, path] = locate(context, hit.notePath);
        QString snippet = hit.snippet;
        snippet.replace(Search::kMarkStart, QStringLiteral("**")).replace(Search::kMarkEnd, QStringLiteral("**"));
        results.append(QJsonObject{{QStringLiteral("root"), root}, {QStringLiteral("path"), path},
                                   {QStringLiteral("title"), hit.title}, {QStringLiteral("snippet"), snippet}});
    }
    return QJsonObject{{QStringLiteral("results"), results}};
}

std::expected<QJsonObject, QString> McpTools::getNote(const Context& context, const QJsonObject& arguments)
{
    const auto root = text(arguments, "root", false);
    const auto path = text(arguments, "path", true);
    if (!root)
        return std::unexpected(root.error());
    if (!path)
        return std::unexpected(path.error());
    const auto absolute = notePath(context, *root, *path);
    if (!absolute)
        return std::unexpected(absolute.error());
    QFile file(*absolute);
    if (!file.open(QIODevice::ReadOnly))
        return std::unexpected(QStringLiteral("Cannot read %1").arg(*path));
    const QByteArray bytes = file.readAll();
    const Frontmatter::Split split = Frontmatter::split(bytes);
    QJsonObject frontmatter;
    if (split.present) {
        ParsedNote parsed;
        Frontmatter::parse(split, parsed);
        for (const Property& property : std::as_const(parsed.properties)) {
            if (property.type == PropertyType::List)
                frontmatter.insert(property.key, QJsonArray::fromStringList(property.values));
            else
                frontmatter.insert(property.key, property.values.value(0));
        }
    }
    const auto [rootName, relative] = locate(context, *absolute);

    QJsonArray links;
    auto db = Database::open(context.indexPath, Database::Mode::ReadOnly);
    if (!db)
        return std::unexpected(db.error());
    auto query = db->prepare("SELECT l.target_raw, l.line, l.embed, l.ambiguous, tr.name, tn.path FROM links l "
                             "JOIN notes s ON s.id = l.src LEFT JOIN notes tn ON tn.id = l.target_note "
                             "LEFT JOIN roots tr ON tr.id = tn.root_id WHERE s.root_id = ? AND s.path = ? ORDER BY l.line, l.rowid");
    if (!query)
        return std::unexpected(query.error());
    query->bind(1, rootId(context, rootName)).bind(2, relative);
    while (query->step().value_or(false)) {
        QJsonObject link{{QStringLiteral("target"), query->text(0)}, {QStringLiteral("line"), int(query->int64(1))}};
        if (query->int64(2))
            link.insert(QStringLiteral("embed"), true);
        if (query->int64(3))
            link.insert(QStringLiteral("ambiguous"), true);
        link.insert(QStringLiteral("resolved"), query->isNull(5) ? QJsonValue()
                                                                 : QJsonValue(QJsonObject{{QStringLiteral("root"), query->text(4)},
                                                                                          {QStringLiteral("path"), query->text(5)}}));
        links.append(link);
    }
    QJsonArray backlinks;
    if (const auto found = Backlinks::collect(context.indexPath, *absolute)) {
        for (const Backlinks::Reference& reference : found->links) {
            const auto [sourceRoot, sourcePath] = locate(context, reference.sourcePath);
            backlinks.append(QJsonObject{{QStringLiteral("root"), sourceRoot}, {QStringLiteral("path"), sourcePath},
                                         {QStringLiteral("line"), reference.line}, {QStringLiteral("context"), reference.context}});
        }
    }
    return QJsonObject{{QStringLiteral("root"), rootName},
                       {QStringLiteral("path"), relative},
                       {QStringLiteral("title"), QFileInfo(relative).completeBaseName()},
                       {QStringLiteral("frontmatter"), frontmatter},
                       {QStringLiteral("body"), QString::fromUtf8(split.present ? split.body : bytes)},
                       {QStringLiteral("links"), links},
                       {QStringLiteral("backlinks"), backlinks}};
}

std::expected<QJsonObject, QString> McpTools::listRegisters(const Context& context, const QJsonObject& arguments)
{
    const auto name = text(arguments, "project", true);
    const auto kind = text(arguments, "kind", false);
    const auto status = text(arguments, "status", false);
    for (const auto* argument : {&name, &kind, &status}) {
        if (!*argument)
            return std::unexpected(argument->error());
    }
    const auto projects = IndexQueries::projects(context.indexPath);
    if (!projects)
        return std::unexpected(projects.error());
    const auto project = findProject(*projects, *name);
    if (!project)
        return std::unexpected(project.error());

    auto db = Database::open(context.indexPath, Database::Mode::ReadOnly);
    if (!db)
        return std::unexpected(db.error());
    const int root = rootId(context, project->rootName);
    QHash<qint64, QJsonObject> fields;
    {
        auto query = db->prepare("SELECT f.reg, f.name, f.value FROM register_fields f JOIN registers g ON g.id = f.reg "
                                 "JOIN projects p ON p.id = g.project_id WHERE p.root_id = ? AND p.path = ? ORDER BY f.rowid");
        if (!query)
            return std::unexpected(query.error());
        query->bind(1, root).bind(2, project->path.isNull() ? QStringLiteral("") : project->path);
        while (query->step().value_or(false)) {
            QJsonObject& entry = fields[query->int64(0)];
            if (!entry.contains(query->text(1)))
                entry.insert(query->text(1), query->text(2));
        }
    }
    auto query = db->prepare("SELECT g.id, g.kind, g.ident, g.title, coalesce(g.status, ''), n.path, g.line FROM registers g "
                             "JOIN projects p ON p.id = g.project_id JOIN notes n ON n.id = g.note "
                             "WHERE p.root_id = ? AND p.path = ? ORDER BY g.kind, g.number, n.path, g.line");
    if (!query)
        return std::unexpected(query.error());
    query->bind(1, root).bind(2, project->path.isNull() ? QStringLiteral("") : project->path);
    QJsonArray entries;
    while (query->step().value_or(false)) {
        const QString entryKind = query->text(1);
        const QString entryStatus = query->text(4);
        if (!kind->isEmpty() && entryKind.compare(*kind, Qt::CaseInsensitive) != 0)
            continue;
        if (!status->isEmpty() && !entryStatus.startsWith(*status, Qt::CaseInsensitive))
            continue;
        entries.append(QJsonObject{{QStringLiteral("ident"), query->text(2)},
                                   {QStringLiteral("title"), query->text(3)},
                                   {QStringLiteral("status"), entryStatus},
                                   {QStringLiteral("file"), query->text(5)},
                                   {QStringLiteral("line"), int(query->int64(6))},
                                   {QStringLiteral("fields"), fields.value(query->int64(0))}});
    }
    return QJsonObject{{QStringLiteral("project"), ProjectOverview::labelOf(*project)},
                       {QStringLiteral("root"), project->rootName},
                       {QStringLiteral("entries"), entries}};
}

std::expected<QJsonObject, QString> McpTools::projectStatus(const Context& context, const QJsonObject& arguments)
{
    const auto name = text(arguments, "project", false);
    if (!name)
        return std::unexpected(name.error());
    const auto projects = IndexQueries::projects(context.indexPath);
    if (!projects)
        return std::unexpected(projects.error());
    QList<ProjectRef> chosen = *projects;
    if (!name->isEmpty()) {
        const auto project = findProject(*projects, *name);
        if (!project)
            return std::unexpected(project.error());
        chosen = {*project};
    }
    const QDate today = QDate::currentDate();
    QJsonArray results;
    for (const ProjectRef& project : std::as_const(chosen)) {
        const ProjectOverview overview = ProjectOverview::of(project, today);
        QJsonObject result{{QStringLiteral("name"), overview.label},
                           {QStringLiteral("root"), project.rootName},
                           {QStringLiteral("status"), project.status},
                           {QStringLiteral("last_reviewed"), project.lastReviewed},
                           {QStringLiteral("phase"), overview.build.summary()},
                           {QStringLiteral("unreleased"), overview.build.hasChangelog ? QJsonValue(overview.build.unreleased) : QJsonValue()},
                           {QStringLiteral("last_release"), overview.build.lastRelease.isEmpty() ? QJsonValue()
                                                                                                 : QJsonValue(overview.build.lastRelease)}};
        if (overview.reviewedDaysAgo >= 0)
            result.insert(QStringLiteral("reviewed_days_ago"), overview.reviewedDaysAgo);
        if (overview.inGit) {
            QJsonObject git{{QStringLiteral("branch"), overview.branch}, {QStringLiteral("dirty"), overview.changes}};
            if (overview.tracking) {
                git.insert(QStringLiteral("upstream"), overview.tracking->upstream);
                git.insert(QStringLiteral("ahead"), overview.tracking->ahead);
                git.insert(QStringLiteral("behind"), overview.tracking->behind);
            }
            result.insert(QStringLiteral("git"), git);
        } else {
            result.insert(QStringLiteral("git"), QJsonValue());
        }
        results.append(result);
    }
    return QJsonObject{{QStringLiteral("projects"), results}};
}

std::expected<QJsonObject, QString> McpTools::listIdeas(const Context& context, const QJsonObject& arguments)
{
    const auto stage = text(arguments, "stage", false);
    if (!stage)
        return std::unexpected(stage.error());
    const auto ideas = IndexQueries::ideas(context.indexPath);
    if (!ideas)
        return std::unexpected(ideas.error());
    QJsonArray results;
    for (const IdeaRef& idea : *ideas) {
        if (!stage->isEmpty() && idea.stage.compare(*stage, Qt::CaseInsensitive) != 0)
            continue;
        results.append(QJsonObject{{QStringLiteral("path"), locate(context, idea.notePath).second},
                                   {QStringLiteral("title"), idea.title},
                                   {QStringLiteral("stage"), idea.stage},
                                   {QStringLiteral("codename"), idea.codename},
                                   {QStringLiteral("repo"), idea.repo},
                                   {QStringLiteral("summary"), idea.summary}});
    }
    return QJsonObject{{QStringLiteral("ideas"), results}};
}

} // namespace coeus
