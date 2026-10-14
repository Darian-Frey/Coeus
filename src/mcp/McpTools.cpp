#include "mcp/McpTools.hpp"

#include "index/Backlinks.hpp"
#include "index/Database.hpp"
#include "index/IndexQueries.hpp"
#include "index/Search.hpp"
#include "index/SearchQuery.hpp"
#include "parser/Frontmatter.hpp"
#include "parser/RegisterParser.hpp"
#include "registers/EntryWriter.hpp"
#include "registers/NextId.hpp"
#include "registers/RegisterTemplates.hpp"
#include "registers/ProjectOverview.hpp"
#include "registers/Vocabulary.hpp"
#include "vault/Vault.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonValue>
#include <QRegularExpression>

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

QJsonObject kindProperty()
{
    return stringProperty(QStringLiteral("The register: F, C, D, AV, BUG or IMP."),
                          {QStringLiteral("F"), QStringLiteral("C"), QStringLiteral("D"), QStringLiteral("AV"), QStringLiteral("BUG"),
                           QStringLiteral("IMP")});
}

QJsonObject writeTool(const QString& name, const QString& title, const QString& description, const QJsonObject& inputSchema,
                      bool destructive)
{
    return {{QStringLiteral("name"), name}, {QStringLiteral("title"), title}, {QStringLiteral("description"), description},
            {QStringLiteral("inputSchema"), inputSchema},
            {QStringLiteral("annotations"), QJsonObject{{QStringLiteral("readOnlyHint"), false},
                                                        {QStringLiteral("destructiveHint"), destructive},
                                                        {QStringLiteral("idempotentHint"), false}}}};
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
        writeTool(QStringLiteral("next_id"), QStringLiteral("Reserve the next register ID"),
                  QStringLiteral("Reserves the next free ID of a kind in a project for 60 seconds and returns it, so a person "
                                 "or another session does not take it meanwhile. append_entry allocates one itself; use this only "
                                 "to mention an ID before writing its entry. Needs writes enabled for the vault."),
                  schema({{QStringLiteral("project"), stringProperty(QStringLiteral("The project, as project_status names it."))},
                          {QStringLiteral("kind"), kindProperty()}},
                         {QStringLiteral("project"), QStringLiteral("kind")}),
                  false),
        writeTool(QStringLiteral("append_entry"), QStringLiteral("Add a register entry"),
                  QStringLiteral("Adds an entry to a project's register (FEATURES, CLAIMS, DECISIONS, ATTACK_VECTORS, BUGS or "
                                 "IMPROVEMENTS), with the next free ID, written to the documentation standard. Fields are given by "
                                 "name (Status, Context, Options, Decision, Consequences, Reversal conditions, Severity, Detection, "
                                 "Trade-offs, Related, …); dates and a starting status are filled in. Refused, with the reason, if "
                                 "a value is outside its vocabulary or a required field is missing (D and C need Reversal "
                                 "conditions, IMP Trade-offs, AV Detection). Needs writes enabled for the vault."),
                  schema({{QStringLiteral("project"), stringProperty(QStringLiteral("The project, as project_status names it."))},
                          {QStringLiteral("kind"), kindProperty()},
                          {QStringLiteral("ident"), stringProperty(QStringLiteral("Optional: the ID expected, e.g. D-021; refused if it is "
                                                                                  "not the next free one."))},
                          {QStringLiteral("title"), stringProperty(QStringLiteral("The entry's title, one line."))},
                          {QStringLiteral("fields"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                                                 {QStringLiteral("additionalProperties"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                                                 {QStringLiteral("description"), QStringLiteral("Field name to text. A list "
                                                                                                                "starts each item with \"- \".")}}}},
                         {QStringLiteral("project"), QStringLiteral("kind"), QStringLiteral("title")}),
                  false),
        writeTool(QStringLiteral("write_note"), QStringLiteral("Write a note"),
                  QStringLiteral("Creates a note, replaces its body, or appends to it. Register files are not written this way: use "
                                 "append_entry. replace keeps the note's frontmatter unless frontmatter is given. Refused while the "
                                 "note is open in Coeus with unsaved changes. Needs writes enabled for the vault."),
                  schema({{QStringLiteral("root"), stringProperty(QStringLiteral("\"vault\" (default) or a linked root's name."))},
                          {QStringLiteral("path"), stringProperty(QStringLiteral("Path within the root; .md is added if missing."))},
                          {QStringLiteral("body"), stringProperty(QStringLiteral("Markdown, without frontmatter."))},
                          {QStringLiteral("frontmatter"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                                                      {QStringLiteral("description"), QStringLiteral("Keys to strings, numbers, "
                                                                                                                     "booleans or lists of "
                                                                                                                     "strings.")}}},
                          {QStringLiteral("mode"), stringProperty(QStringLiteral("create (fails if it exists), replace or append."),
                                                                  {QStringLiteral("create"), QStringLiteral("replace"), QStringLiteral("append")})}},
                         {QStringLiteral("path"), QStringLiteral("body"), QStringLiteral("mode")}),
                  true),
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
    if (isWriteTool(name) && !context.allowWrites)
        return std::unexpected(writesDisabled(context.vaultPath));
    if (name == QLatin1String("next_id"))
        return nextId(context, arguments);
    if (name == QLatin1String("append_entry"))
        return appendEntry(context, arguments);
    if (name == QLatin1String("write_note"))
        return writeNote(context, arguments);
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

bool McpTools::isWriteTool(const QString& name)
{
    return name == QLatin1String("next_id") || name == QLatin1String("append_entry") || name == QLatin1String("write_note");
}

QString McpTools::writesDisabled(const QString& vaultPath)
{
    return QStringLiteral("Writing is turned off for this vault, as it is by default. The author can allow it by adding to "
                          "%1/.coeus/config.toml:\n\n[mcp]\nallow_writes = true")
        .arg(vaultPath);
}

namespace {

// The register file of `kind` in `project`, as NextId would create it.
QString registerFile(const ProjectRef& project, const QString& kind)
{
    const QString folder = project.path.isEmpty() ? project.rootPath : project.rootPath + QLatin1Char('/') + project.path;
    return folder + QLatin1Char('/') + RegisterTemplates::fileName(kind);
}

// Refused while the note is open in the window with unsaved changes (AV-012).
std::optional<QString> openWithEdits(const McpTools::Context& context, const QString& path, QString* unsavedText = nullptr)
{
    if (!context.gui)
        return std::nullopt;
    const auto buffer = context.gui->buffer(path);
    if (!buffer || !buffer->modified)
        return std::nullopt;
    if (unsavedText)
        *unsavedText = buffer->text;
    return QStringLiteral("%1 is open in Coeus with unsaved changes; the author must save or discard them first")
        .arg(QFileInfo(path).fileName());
}

std::expected<QString, QString> kindArgument(const QJsonObject& arguments)
{
    const auto kind = text(arguments, "kind", true);
    if (!kind)
        return kind;
    const QString upper = kind->toUpper();
    if (!RegisterTemplates::kinds().contains(upper))
        return std::unexpected(QStringLiteral("`kind` is one of F, C, D, AV, BUG, IMP"));
    return upper;
}

std::expected<NextId::Allocation, QString> allocate(const McpTools::Context& context, const ProjectRef& project, const QString& kind,
                                                     const QString& unsaved)
{
    NextId::Request request;
    request.indexPath = context.indexPath;
    request.rootPath = project.rootPath;
    request.projectPath = project.path;
    request.kind = kind;
    request.holder = context.holder;
    request.unsavedText = unsaved;
    return NextId::allocate(request);
}

std::expected<void, QString> writeFile(const McpTools::Context& context, const QString& path, const QByteArray& content,
                                        const QString& summary)
{
    std::optional<QByteArray> before;
    if (QFile existing(path); existing.exists()) {
        if (!existing.open(QIODevice::ReadOnly))
            return std::unexpected(QStringLiteral("Cannot read %1").arg(path));
        before = existing.readAll();
    }
    const auto [rootName, relative] = locate(context, path);
    const IndexRoots::Root* root = rootNamed(context, rootName);
    const auto vault = root ? Vault::open(root->info.path) : std::expected<Vault, QString>(std::unexpected(QStringLiteral("no root")));
    if (!vault)
        return std::unexpected(QStringLiteral("%1 is outside every root").arg(path));
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return std::unexpected(QStringLiteral("Cannot create %1").arg(QFileInfo(path).absolutePath()));
    if (const auto written = vault->writeNote(path, content); !written)
        return std::unexpected(written.error());
    if (context.gui)
        context.gui->wrote(path, before, content, summary); // the window offers an undo
    return {};
}

} // namespace

std::expected<QJsonObject, QString> McpTools::nextId(const Context& context, const QJsonObject& arguments)
{
    const auto name = text(arguments, "project", true);
    if (!name)
        return std::unexpected(name.error());
    const auto kind = kindArgument(arguments);
    if (!kind)
        return std::unexpected(kind.error());
    const auto projects = IndexQueries::projects(context.indexPath);
    if (!projects)
        return std::unexpected(projects.error());
    const auto project = findProject(*projects, *name);
    if (!project)
        return std::unexpected(project.error());
    QString unsaved;
    openWithEdits(context, registerFile(*project, *kind), &unsaved); // its unsaved IDs count, as in the window
    const auto allocation = allocate(context, *project, *kind, unsaved);
    if (!allocation)
        return std::unexpected(allocation.error());
    return QJsonObject{{QStringLiteral("ident"), allocation->ident},
                       {QStringLiteral("project"), ProjectOverview::labelOf(*project)},
                       {QStringLiteral("file"), locate(context, allocation->registerFile).second},
                       {QStringLiteral("reserved_seconds"), NextId::kReservationSeconds}};
}

std::expected<QJsonObject, QString> McpTools::appendEntry(const Context& context, const QJsonObject& arguments)
{
    const auto name = text(arguments, "project", true);
    const auto ident = text(arguments, "ident", false);
    const auto title = text(arguments, "title", true);
    for (const auto* argument : {&name, &ident, &title}) {
        if (!*argument)
            return std::unexpected(argument->error());
    }
    const auto kind = kindArgument(arguments);
    if (!kind)
        return std::unexpected(kind.error());
    const QJsonValue fieldsValue = arguments.value(QStringLiteral("fields"));
    if (!fieldsValue.isUndefined() && !fieldsValue.isNull() && !fieldsValue.isObject())
        return std::unexpected(QStringLiteral("`fields` is an object of field name to text"));
    QList<QPair<QString, QString>> fields;
    const QJsonObject fieldObject = fieldsValue.toObject();
    for (auto it = fieldObject.constBegin(); it != fieldObject.constEnd(); ++it) {
        if (!it.value().isString())
            return std::unexpected(QStringLiteral("The field %1 must be text").arg(it.key()));
        fields.append({it.key(), it.value().toString()});
    }
    const auto projects = IndexQueries::projects(context.indexPath);
    if (!projects)
        return std::unexpected(projects.error());
    const auto project = findProject(*projects, *name);
    if (!project)
        return std::unexpected(project.error());

    // Everything that can be checked is checked before an ID is reserved, so a
    // refusal leaves no gap in the numbering.
    const QDate today = QDate::currentDate();
    if (const auto trial = EntryWriter::render({*kind, *kind + QStringLiteral("-000"), *title, fields}, today); !trial)
        return std::unexpected(trial.error());
    if (const auto busy = openWithEdits(context, registerFile(*project, *kind)))
        return std::unexpected(*busy);

    const auto allocation = allocate(context, *project, *kind, QString());
    if (!allocation)
        return std::unexpected(allocation.error());
    if (!ident->isEmpty() && RegisterParser::canonical(*ident) != allocation->ident)
        return std::unexpected(QStringLiteral("%1 is not the next free %2 ID in %3: that is %4 (IDs are never reused or skipped, AV-003)")
                                   .arg(*ident, *kind, ProjectOverview::labelOf(*project), allocation->ident));
    const auto entry = EntryWriter::render({*kind, allocation->ident, *title, fields}, today);
    if (!entry)
        return std::unexpected(entry.error());

    const QString path = allocation->registerFile;
    QString current = RegisterTemplates::fileSkeleton(*kind);
    if (allocation->fileExists) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return std::unexpected(QStringLiteral("Cannot read %1").arg(path));
        current = QString::fromUtf8(file.readAll());
        // The last guard (SPEC.md §ID allocation): the file as it is now must not hold the ID.
        for (const RegisterEntry& existing : RegisterParser::parse(current)) {
            if (existing.ident == allocation->ident)
                return std::unexpected(QStringLiteral("%1 appeared in %2 meanwhile; nothing was written").arg(allocation->ident, path));
        }
    }
    const QString status = RegisterParser::parse(*entry).value(0).status; // where a BUG or IMP entry goes
    const EntryWriter::Placed placed = EntryWriter::place(current, *kind, status, *entry);
    const QString file = locate(context, path).second;
    if (const auto written = writeFile(context, path, placed.text.toUtf8(),
                                       QStringLiteral("An AI session added %1 %2 to %3").arg(allocation->ident, *title, file));
        !written)
        return std::unexpected(written.error());
    return QJsonObject{{QStringLiteral("ident"), allocation->ident},
                       {QStringLiteral("root"), project->rootName},
                       {QStringLiteral("path"), file},
                       {QStringLiteral("line"), placed.line},
                       {QStringLiteral("created_file"), !allocation->fileExists}};
}

std::expected<QJsonObject, QString> McpTools::writeNote(const Context& context, const QJsonObject& arguments)
{
    static const QRegularExpression kKey(QStringLiteral(R"(^[A-Za-z0-9_][A-Za-z0-9_-]*$)"));
    constexpr qsizetype kMaxBytes = 1 << 20;
    const auto rootName = text(arguments, "root", false);
    const auto pathArgument = text(arguments, "path", true);
    const auto mode = text(arguments, "mode", true);
    for (const auto* argument : {&rootName, &pathArgument, &mode}) {
        if (!*argument)
            return std::unexpected(argument->error());
    }
    const QJsonValue bodyValue = arguments.value(QStringLiteral("body"));
    if (!bodyValue.isString())
        return std::unexpected(QStringLiteral("`body` is required text"));
    QString body = bodyValue.toString();
    body.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    if (*mode != QLatin1String("create") && *mode != QLatin1String("replace") && *mode != QLatin1String("append"))
        return std::unexpected(QStringLiteral("`mode` is create, replace or append"));

    const IndexRoots::Root* root = rootNamed(context, rootName->isEmpty() ? QStringLiteral("vault") : *rootName);
    if (!root || !root->present)
        return std::unexpected(QStringLiteral("No root \"%1\" here; the roots are: %2").arg(*rootName, rootNames(context)));
    QString relative = QDir::cleanPath(*pathArgument);
    if (!relative.endsWith(QLatin1String(".md"), Qt::CaseInsensitive))
        relative += QStringLiteral(".md");
    const QStringList segments = relative.split(QLatin1Char('/'));
    const bool hidden = std::any_of(segments.begin(), segments.end(), [](const QString& s) { return s.startsWith(QLatin1Char('.')); });
    if (QDir::isAbsolutePath(relative) || relative.startsWith(QLatin1String("../")) || hidden)
        return std::unexpected(QStringLiteral("`path` must be a visible path inside the root, such as notes/plan.md"));
    if (RegisterParser::isRegisterFile(segments.back()))
        return std::unexpected(QStringLiteral("%1 is a register; add entries with append_entry, which keeps IDs whole (AV-003)")
                                   .arg(segments.back()));
    const QString path = root->info.path + QLatin1Char('/') + relative;
    const bool exists = QFileInfo::exists(path);
    if (*mode == QLatin1String("create") && exists)
        return std::unexpected(QStringLiteral("%1 already exists; use replace or append").arg(relative));
    if (*mode != QLatin1String("create") && !exists)
        return std::unexpected(QStringLiteral("No note %1 to %2; use create").arg(relative, *mode));
    if (const auto busy = openWithEdits(context, path))
        return std::unexpected(*busy);

    // Frontmatter, as YAML the parser reads back the same.
    const QJsonValue frontmatterValue = arguments.value(QStringLiteral("frontmatter"));
    if (!frontmatterValue.isUndefined() && !frontmatterValue.isNull() && !frontmatterValue.isObject())
        return std::unexpected(QStringLiteral("`frontmatter` is an object"));
    if (frontmatterValue.isObject() && *mode == QLatin1String("append"))
        return std::unexpected(QStringLiteral("append adds to the body; leave frontmatter out"));
    QString yaml;
    const QJsonObject frontmatter = frontmatterValue.toObject();
    for (auto it = frontmatter.constBegin(); it != frontmatter.constEnd(); ++it) {
        if (!kKey.match(it.key()).hasMatch())
            return std::unexpected(QStringLiteral("\"%1\" is not a frontmatter key").arg(it.key()));
        const QJsonValue value = it.value();
        QString rendered;
        if (value.isArray()) {
            QStringList items;
            for (const QJsonValue& item : value.toArray()) {
                if (!item.isString())
                    return std::unexpected(QStringLiteral("The list %1 must hold text").arg(it.key()));
                items.append(item.toString());
            }
            rendered = Frontmatter::yamlList(items);
        } else if (value.isString()) {
            rendered = Frontmatter::yamlScalar(value.toString());
        } else if (value.isDouble()) {
            rendered = QString::number(value.toDouble());
        } else if (value.isBool()) {
            rendered = value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
        } else {
            return std::unexpected(QStringLiteral("%1 must be text, a number, true/false or a list of text").arg(it.key()));
        }
        yaml += it.key() + QStringLiteral(": ") + rendered + QLatin1Char('\n');
    }

    QString content;
    if (*mode == QLatin1String("append")) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return std::unexpected(QStringLiteral("Cannot read %1").arg(relative));
        content = QString::fromUtf8(file.readAll());
        if (!content.isEmpty() && !content.endsWith(QLatin1Char('\n')))
            content += QLatin1Char('\n');
        if (!content.isEmpty())
            content += QLatin1Char('\n');
        content += body;
    } else {
        QString keep;
        if (*mode == QLatin1String("replace") && !frontmatterValue.isObject()) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                return std::unexpected(QStringLiteral("Cannot read %1").arg(relative));
            const QByteArray bytes = file.readAll();
            const Frontmatter::Split split = Frontmatter::split(bytes);
            if (split.present)
                keep = QString::fromUtf8(bytes.left(bytes.size() - split.body.size())); // its frontmatter as written
        }
        content = !keep.isEmpty() ? keep + body : yaml.isEmpty() ? body : QStringLiteral("---\n") + yaml + QStringLiteral("---\n") + body;
    }
    if (!content.endsWith(QLatin1Char('\n')))
        content += QLatin1Char('\n');
    const QByteArray bytes = content.toUtf8();
    if (bytes.size() > kMaxBytes)
        return std::unexpected(QStringLiteral("The note would be over 1 MB; write less"));
    const QString verb = *mode == QLatin1String("create") ? QStringLiteral("created") : *mode == QLatin1String("append") ? QStringLiteral("added to")
                                                                                                                        : QStringLiteral("rewrote");
    if (const auto written = writeFile(context, path, bytes, QStringLiteral("An AI session %1 %2").arg(verb, relative)); !written)
        return std::unexpected(written.error());
    return QJsonObject{{QStringLiteral("root"), root->info.name}, {QStringLiteral("path"), relative}, {QStringLiteral("mode"), *mode},
                       {QStringLiteral("bytes"), int(bytes.size())}};
}

} // namespace coeus
