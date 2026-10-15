#include "git/GitRepository.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>

#include <git2.h>

#include <limits>

namespace coeus {

namespace {

QString lastError(const char* what)
{
    const git_error* error = git_error_last();
    return QStringLiteral("%1: %2").arg(QLatin1String(what), error && error->message ? QString::fromUtf8(error->message)
                                                                                     : QStringLiteral("unknown error"));
}

template <typename T, void (*FreeFn)(T*)>
struct Owned {
    T* ptr = nullptr;
    Owned() = default;
    Owned(const Owned&) = delete;
    Owned& operator=(const Owned&) = delete;
    ~Owned() { FreeFn(ptr); }
    T** out() { return &ptr; }
    T* get() const { return ptr; }
};

using Commit = Owned<git_commit, git_commit_free>;
using Tree = Owned<git_tree, git_tree_free>;
using Entry = Owned<git_tree_entry, git_tree_entry_free>;
using Walk = Owned<git_revwalk, git_revwalk_free>;
using Diff = Owned<git_diff, git_diff_free>;
using Object = Owned<git_object, git_object_free>;
using Blob = Owned<git_blob, git_blob_free>;
using Reference = Owned<git_reference, git_reference_free>;
using Blame = Owned<git_blame, git_blame_free>;
using Index = Owned<git_index, git_index_free>;
using StatusList = Owned<git_status_list, git_status_list_free>;
using Signature = Owned<git_signature, git_signature_free>;

// Coeus's own files, never offered for a commit (D-011): the index, and the
// embeddings sidecar, which syncs by the sync provider and is rewritten after
// every embedding pass (BUG-008).
bool isCoeusFile(const QString& path)
{
    for (const QLatin1String folder : {QLatin1String(".coeus"), QLatin1String(".coeus-shared")}) {
        if (path == folder || path.startsWith(folder + QLatin1Char('/')) || path.contains(QLatin1Char('/') + folder + QLatin1Char('/')))
            return true;
    }
    const QString name = path.section(QLatin1Char('/'), -1);
    return name.startsWith(QLatin1Char('.')) && name.contains(QLatin1String(".coeus-tmp-"));
}

QString hex(const git_oid* oid)
{
    char buffer[GIT_OID_HEXSZ + 1] = {};
    git_oid_tostr(buffer, sizeof buffer, oid);
    return QString::fromLatin1(buffer);
}

// The blob id of `path` in `tree`, if it is a file there.
bool blobIn(git_tree* tree, const QString& path, git_oid* oid)
{
    Entry entry;
    if (git_tree_entry_bypath(entry.out(), tree, path.toUtf8().constData()) != 0)
        return false;
    if (git_tree_entry_type(entry.get()) != GIT_OBJECT_BLOB)
        return false;
    git_oid_cpy(oid, git_tree_entry_id(entry.get()));
    return true;
}

// The path `path` had in `parentTree`, if `tree` got it by renaming it.
QString renamedFrom(git_repository* repo, git_tree* parentTree, git_tree* tree, const QString& path)
{
    Diff diff;
    if (git_diff_tree_to_tree(diff.out(), repo, parentTree, tree, nullptr) != 0)
        return {};
    git_diff_find_options find = GIT_DIFF_FIND_OPTIONS_INIT;
    find.flags = GIT_DIFF_FIND_RENAMES;
    if (git_diff_find_similar(diff.get(), &find) != 0)
        return {};
    const QByteArray wanted = path.toUtf8();
    for (size_t i = 0; i < git_diff_num_deltas(diff.get()); ++i) {
        const git_diff_delta* delta = git_diff_get_delta(diff.get(), i);
        if (delta->status == GIT_DELTA_RENAMED && wanted == delta->new_file.path)
            return QString::fromUtf8(delta->old_file.path);
    }
    return {};
}

GitCommit describe(git_commit* commit, const QString& path)
{
    GitCommit out;
    out.id = hex(git_commit_id(commit));
    out.shortId = out.id.left(7);
    out.summary = QString::fromUtf8(git_commit_summary(commit));
    out.message = QString::fromUtf8(git_commit_message(commit)).trimmed();
    const git_signature* author = git_commit_author(commit);
    out.author = QString::fromUtf8(author->name);
    out.email = QString::fromUtf8(author->email);
    out.time = QDateTime::fromSecsSinceEpoch(author->when.time, Qt::OffsetFromUTC, author->when.offset * 60);
    out.path = path;
    return out;
}

} // namespace

void GitRepository::Free::operator()(git_repository* repo) const
{
    git_repository_free(repo);
    git_libgit2_shutdown();
}

GitRepository::GitRepository(git_repository* repo, QString workdir)
    : m_repo(repo)
    , m_workdir(std::move(workdir))
{
}

GitRepository::~GitRepository() = default;

std::expected<GitRepository, QString> GitRepository::discover(const QString& path)
{
    git_libgit2_init();
    git_repository* repo = nullptr;
    const QString start = QFileInfo(path).isDir() ? path : QFileInfo(path).absolutePath();
    if (git_repository_open_ext(&repo, QFile::encodeName(start).constData(), 0, nullptr) != 0) {
        git_libgit2_shutdown();
        return std::unexpected(QStringLiteral("Not in a Git repository"));
    }
    if (git_repository_is_bare(repo) || !git_repository_workdir(repo)) {
        git_repository_free(repo);
        git_libgit2_shutdown();
        return std::unexpected(QStringLiteral("A bare repository has no working tree"));
    }
    QString workdir = QFileInfo(QFile::decodeName(git_repository_workdir(repo))).canonicalFilePath();
    return GitRepository(repo, workdir);
}

QString GitRepository::relativePath(const QString& absolutePath) const
{
    const QFileInfo info(absolutePath);
    const QString canonical = info.exists() ? info.canonicalFilePath()
                                            : QFileInfo(info.absolutePath()).canonicalFilePath() + QLatin1Char('/') + info.fileName();
    return QDir(m_workdir).relativeFilePath(canonical);
}

QString GitRepository::branch() const
{
    Reference head;
    const int rc = git_repository_head(head.out(), m_repo.get());
    if (rc == GIT_EUNBORNBRANCH) {
        // Before the first commit HEAD names a branch that does not exist yet.
        Reference symbolic;
        if (git_reference_lookup(symbolic.out(), m_repo.get(), "HEAD") == 0 && git_reference_symbolic_target(symbolic.get())) {
            const QString target = QString::fromUtf8(git_reference_symbolic_target(symbolic.get()));
            return target.startsWith(QLatin1String("refs/heads/")) ? target.mid(11) : target;
        }
        return {};
    }
    if (rc != 0)
        return {};
    if (git_repository_head_detached(m_repo.get()) == 1) {
        const git_oid* oid = git_reference_target(head.get());
        return oid ? QStringLiteral("detached at %1").arg(hex(oid).left(7)) : QStringLiteral("detached");
    }
    return QString::fromUtf8(git_reference_shorthand(head.get()));
}

std::expected<QList<GitCommit>, QString> GitRepository::fileHistory(const QString& relativePath, int limit) const
{
    QList<GitCommit> history;
    if (git_repository_head_unborn(m_repo.get()) == 1)
        return history; // no commits yet
    Walk walk;
    if (git_revwalk_new(walk.out(), m_repo.get()) != 0)
        return std::unexpected(lastError("Cannot walk the history"));
    git_revwalk_sorting(walk.get(), GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME);
    if (git_revwalk_push_head(walk.get()) != 0)
        return std::unexpected(lastError("Cannot read HEAD"));

    QString path = relativePath;
    git_oid oid;
    while (history.size() < limit && git_revwalk_next(&oid, walk.get()) == 0) {
        Commit commit;
        Tree tree;
        if (git_commit_lookup(commit.out(), m_repo.get(), &oid) != 0 || git_commit_tree(tree.out(), commit.get()) != 0)
            return std::unexpected(lastError("Cannot read a commit"));
        git_oid blob;
        if (!blobIn(tree.get(), path, &blob))
            continue; // not on this path here: before it was added, or on another line of history

        const unsigned parents = git_commit_parentcount(commit.get());
        bool changed = true;
        bool inAnyParent = false;
        Tree firstParentTree;
        for (unsigned i = 0; i < parents; ++i) {
            Commit parent;
            Tree parentTree;
            if (git_commit_parent(parent.out(), commit.get(), i) != 0 || git_commit_tree(parentTree.out(), parent.get()) != 0)
                return std::unexpected(lastError("Cannot read a parent commit"));
            git_oid parentBlob;
            if (blobIn(parentTree.get(), path, &parentBlob)) {
                inAnyParent = true;
                if (git_oid_equal(&parentBlob, &blob)) {
                    changed = false; // the same as a parent's: not this commit's change
                    break;
                }
            }
            if (i == 0)
                std::swap(firstParentTree.ptr, parentTree.ptr);
        }
        if (!changed)
            continue;
        history.append(describe(commit.get(), path));
        // Added here: if by renaming, older commits know it by its old name.
        if (!inAnyParent && firstParentTree.get()) {
            const QString old = renamedFrom(m_repo.get(), firstParentTree.get(), tree.get(), path);
            if (!old.isEmpty())
                path = old;
        }
    }
    return history;
}

std::expected<QByteArray, QString> GitRepository::fileAt(const QString& commitId, const QString& relativePath) const
{
    Object object;
    if (git_revparse_single(object.out(), m_repo.get(), commitId.toUtf8().constData()) != 0)
        return std::unexpected(lastError("No such commit"));
    Object peeled;
    if (git_object_peel(peeled.out(), object.get(), GIT_OBJECT_COMMIT) != 0)
        return std::unexpected(lastError("Not a commit"));
    Tree tree;
    if (git_commit_tree(tree.out(), reinterpret_cast<git_commit*>(peeled.get())) != 0)
        return std::unexpected(lastError("Cannot read the commit's tree"));
    git_oid oid;
    if (!blobIn(tree.get(), relativePath, &oid))
        return std::unexpected(QStringLiteral("%1 is not in commit %2").arg(relativePath, commitId.left(7)));
    Blob blob;
    if (git_blob_lookup(blob.out(), m_repo.get(), &oid) != 0)
        return std::unexpected(lastError("Cannot read the file"));
    return QByteArray(static_cast<const char*>(git_blob_rawcontent(blob.get())), qsizetype(git_blob_rawsize(blob.get())));
}

QString GitChange::letter() const
{
    switch (kind) {
    case Kind::Added: return QStringLiteral("A");
    case Kind::Modified: return QStringLiteral("M");
    case Kind::Deleted: return QStringLiteral("D");
    case Kind::Renamed: return QStringLiteral("R");
    case Kind::TypeChanged: return QStringLiteral("T");
    case Kind::Untracked: return QStringLiteral("?");
    case Kind::Conflicted: return QStringLiteral("U");
    }
    return QStringLiteral("M");
}

std::expected<QList<GitChange>, QString> GitRepository::status() const
{
    git_status_options options = GIT_STATUS_OPTIONS_INIT;
    options.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    options.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS
                    | GIT_STATUS_OPT_EXCLUDE_SUBMODULES | GIT_STATUS_OPT_SORT_CASE_SENSITIVELY;
    StatusList list;
    if (git_status_list_new(list.out(), m_repo.get(), &options) != 0)
        return std::unexpected(lastError("Cannot read the status"));
    QList<GitChange> changes;
    for (size_t i = 0; i < git_status_list_entrycount(list.get()); ++i) {
        const git_status_entry* entry = git_status_byindex(list.get(), i);
        const unsigned flags = entry->status;
        if (flags == GIT_STATUS_CURRENT || (flags & GIT_STATUS_IGNORED))
            continue;
        const git_diff_delta* delta = entry->head_to_index ? entry->head_to_index : entry->index_to_workdir;
        if (!delta)
            continue;
        GitChange change;
        change.path = QString::fromUtf8(delta->new_file.path ? delta->new_file.path : delta->old_file.path);
        if (isCoeusFile(change.path))
            continue;
        change.staged = flags & (GIT_STATUS_INDEX_NEW | GIT_STATUS_INDEX_MODIFIED | GIT_STATUS_INDEX_DELETED
                                 | GIT_STATUS_INDEX_RENAMED | GIT_STATUS_INDEX_TYPECHANGE);
        change.unstaged = flags & (GIT_STATUS_WT_NEW | GIT_STATUS_WT_MODIFIED | GIT_STATUS_WT_DELETED
                                   | GIT_STATUS_WT_RENAMED | GIT_STATUS_WT_TYPECHANGE);
        if (flags & GIT_STATUS_CONFLICTED)
            change.kind = GitChange::Kind::Conflicted;
        else if (flags & (GIT_STATUS_INDEX_DELETED | GIT_STATUS_WT_DELETED))
            change.kind = GitChange::Kind::Deleted;
        else if (flags & GIT_STATUS_INDEX_NEW)
            change.kind = GitChange::Kind::Added;
        else if (flags & GIT_STATUS_WT_NEW)
            change.kind = GitChange::Kind::Untracked;
        else if (flags & (GIT_STATUS_INDEX_RENAMED | GIT_STATUS_WT_RENAMED))
            change.kind = GitChange::Kind::Renamed;
        else if (flags & (GIT_STATUS_INDEX_TYPECHANGE | GIT_STATUS_WT_TYPECHANGE))
            change.kind = GitChange::Kind::TypeChanged;
        else
            change.kind = GitChange::Kind::Modified;
        changes.append(change);
    }
    return changes;
}

std::expected<GitCommit, QString> GitRepository::commit(const QStringList& paths, const QString& message)
{
    if (paths.isEmpty())
        return std::unexpected(QStringLiteral("Nothing selected to commit"));
    if (message.trimmed().isEmpty())
        return std::unexpected(QStringLiteral("The commit message is empty"));
    for (const QString& path : paths) {
        if (isCoeusFile(path) || path.startsWith(QLatin1String("../")) || QDir::isAbsolutePath(path))
            return std::unexpected(QStringLiteral("Not a path that can be committed: %1").arg(path));
    }
    Signature author;
    if (git_signature_default(author.out(), m_repo.get()) != 0)
        return std::unexpected(QStringLiteral("Git does not know who you are: set user.name and user.email (git config)"));

    // 1. Stage the chosen paths in the repository's index, as `git add` would.
    Index index;
    if (git_repository_index(index.out(), m_repo.get()) != 0)
        return std::unexpected(lastError("Cannot read the index"));
    if (git_index_has_conflicts(index.get()))
        return std::unexpected(QStringLiteral("The repository has unresolved conflicts; resolve them with git first"));
    for (const QString& path : paths) {
        const QByteArray raw = path.toUtf8();
        const bool exists = QFileInfo::exists(m_workdir + QLatin1Char('/') + path) || QFileInfo(m_workdir + QLatin1Char('/') + path).isSymLink();
        const int rc = exists ? git_index_add_bypath(index.get(), raw.constData()) : git_index_remove_bypath(index.get(), raw.constData());
        if (rc != 0)
            return std::unexpected(lastError(exists ? "Cannot stage a file" : "Cannot stage a deletion"));
    }
    if (git_index_write(index.get()) != 0)
        return std::unexpected(lastError("Cannot write the index"));

    // 2. The commit's tree: HEAD's, with only those paths taken from the index.
    Commit parent;
    Tree parentTree;
    const bool unborn = git_repository_head_unborn(m_repo.get()) == 1;
    if (!unborn) {
        Object head;
        if (git_revparse_single(head.out(), m_repo.get(), "HEAD^{commit}") != 0
            || git_commit_lookup(parent.out(), m_repo.get(), git_object_id(head.get())) != 0
            || git_commit_tree(parentTree.out(), parent.get()) != 0)
            return std::unexpected(lastError("Cannot read HEAD"));
    }
    Index partial;
    if (git_index_new(partial.out()) != 0)
        return std::unexpected(lastError("Cannot build the commit"));
    if (parentTree.get() && git_index_read_tree(partial.get(), parentTree.get()) != 0)
        return std::unexpected(lastError("Cannot read HEAD's tree"));
    for (const QString& path : paths) {
        const QByteArray raw = path.toUtf8();
        if (const git_index_entry* staged = git_index_get_bypath(index.get(), raw.constData(), 0)) {
            if (git_index_add(partial.get(), staged) != 0)
                return std::unexpected(lastError("Cannot build the commit"));
        } else {
            git_index_remove(partial.get(), raw.constData(), 0); // deleted; absent from HEAD is fine too
        }
    }
    git_oid treeId;
    if (git_index_write_tree_to(&treeId, partial.get(), m_repo.get()) != 0)
        return std::unexpected(lastError("Cannot write the tree"));
    if (parentTree.get() && git_oid_equal(&treeId, git_tree_id(parentTree.get())))
        return std::unexpected(QStringLiteral("The selected files are unchanged since HEAD"));
    Tree tree;
    if (git_tree_lookup(tree.out(), m_repo.get(), &treeId) != 0)
        return std::unexpected(lastError("Cannot read the new tree"));

    // 3. The commit, moving the branch HEAD names.
    QString text = message.trimmed();
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    const QByteArray encoded = (text + QLatin1Char('\n')).toUtf8();
    const git_commit* parents[] = {parent.get()};
    git_oid id;
    if (git_commit_create(&id, m_repo.get(), "HEAD", author.get(), author.get(), nullptr, encoded.constData(), tree.get(),
                          unborn ? 0 : 1, parents)
        != 0)
        return std::unexpected(lastError("Cannot commit"));
    Commit created;
    if (git_commit_lookup(created.out(), m_repo.get(), &id) != 0)
        return std::unexpected(lastError("Cannot read the new commit"));
    return describe(created.get(), paths.size() == 1 ? paths.front() : QString());
}

std::optional<GitRepository::Tracking> GitRepository::tracking() const
{
    if (git_repository_head_unborn(m_repo.get()) == 1 || git_repository_head_detached(m_repo.get()) == 1)
        return std::nullopt;
    Reference head;
    Reference upstream;
    if (git_repository_head(head.out(), m_repo.get()) != 0 || git_branch_upstream(upstream.out(), head.get()) != 0)
        return std::nullopt;
    const git_oid* local = git_reference_target(head.get());
    const git_oid* remote = git_reference_target(upstream.get());
    size_t ahead = 0;
    size_t behind = 0;
    if (!local || !remote || git_graph_ahead_behind(&ahead, &behind, m_repo.get(), local, remote) != 0)
        return std::nullopt;
    return Tracking{QString::fromUtf8(git_reference_shorthand(upstream.get())), int(ahead), int(behind)};
}

std::expected<QList<BlameLine>, QString> GitRepository::blame(const QString& relativePath, const QByteArray& buffer) const
{
    // Lines of the buffer, as libgit2 counts them.
    const int lineCount = int(buffer.count('\n')) + (buffer.isEmpty() || buffer.endsWith('\n') ? 0 : 1);
    QList<BlameLine> lines(lineCount);

    Blame committed;
    git_blame_options options = GIT_BLAME_OPTIONS_INIT;
    const int rc = git_blame_file(committed.out(), m_repo.get(), relativePath.toUtf8().constData(), &options);
    if (rc == GIT_ENOTFOUND || git_repository_head_unborn(m_repo.get()) == 1)
        return lines; // not in HEAD: nothing is committed yet
    if (rc != 0)
        return std::unexpected(lastError("Cannot blame the file"));
    Blame current;
    if (git_blame_buffer(current.out(), committed.get(), buffer.constData(), size_t(buffer.size())) != 0)
        return std::unexpected(lastError("Cannot blame the buffer"));

    QHash<QString, BlameLine> commits; // one lookup per commit
    const uint32_t hunks = git_blame_get_hunk_count(current.get());
    for (uint32_t i = 0; i < hunks; ++i) {
        const git_blame_hunk* hunk = git_blame_get_hunk_byindex(current.get(), i);
        if (git_oid_is_zero(&hunk->final_commit_id))
            continue; // added in the buffer
        const QString id = hex(&hunk->final_commit_id);
        auto known = commits.find(id);
        if (known == commits.end()) {
            BlameLine line;
            line.id = id;
            line.shortId = id.left(7);
            Commit commit;
            if (git_commit_lookup(commit.out(), m_repo.get(), &hunk->final_commit_id) == 0) {
                const GitCommit described = describe(commit.get(), relativePath);
                line.summary = described.summary;
                line.author = described.author;
                line.time = described.time;
            }
            known = commits.insert(id, line);
        }
        const int first = int(hunk->final_start_line_number) - 1;
        for (int l = first; l < first + int(hunk->lines_in_hunk) && l < lineCount; ++l) {
            if (l >= 0)
                lines[l] = *known;
        }
    }
    return lines;
}

std::expected<QList<DiffLine>, QString> GitRepository::diff(const QByteArray& oldText, const QByteArray& newText)
{
    git_libgit2_init();
    git_diff_options options = GIT_DIFF_OPTIONS_INIT;
    options.context_lines = std::numeric_limits<uint32_t>::max() / 2; // the whole file as one hunk
    options.flags = GIT_DIFF_FORCE_TEXT;
    struct Collect {
        QList<DiffLine> lines;
        bool sawHunk = false;
    } collect;
    const auto onLine = [](const git_diff_delta*, const git_diff_hunk*, const git_diff_line* line, void* payload) -> int {
        auto* out = static_cast<Collect*>(payload);
        DiffLine diffLine;
        switch (line->origin) {
        case GIT_DIFF_LINE_CONTEXT: diffLine.kind = DiffLine::Kind::Context; break;
        case GIT_DIFF_LINE_ADDITION: diffLine.kind = DiffLine::Kind::Added; break;
        case GIT_DIFF_LINE_DELETION: diffLine.kind = DiffLine::Kind::Removed; break;
        default: return 0; // "\ No newline at end of file" and file headers
        }
        diffLine.oldLine = std::max(0, line->old_lineno);
        diffLine.newLine = std::max(0, line->new_lineno);
        QByteArray text(line->content, qsizetype(line->content_len));
        while (text.endsWith('\n') || text.endsWith('\r'))
            text.chop(1);
        diffLine.text = QString::fromUtf8(text);
        out->lines.append(diffLine);
        return 0;
    };
    const int rc = git_diff_buffers(oldText.constData(), size_t(oldText.size()), nullptr, newText.constData(),
                                    size_t(newText.size()), nullptr, &options, nullptr, nullptr, nullptr, onLine, &collect);
    const QString error = rc == 0 ? QString() : lastError("Cannot compare the texts");
    git_libgit2_shutdown();
    if (rc != 0)
        return std::unexpected(error);
    if (collect.lines.isEmpty() && !newText.isEmpty()) {
        // Identical: libgit2 reports no hunk, so list the text unchanged.
        int n = 0;
        for (const QByteArray& line : newText.split('\n')) {
            if (n == int(newText.count('\n')) && line.isEmpty())
                break; // the empty piece after a final newline
            ++n;
            collect.lines.append({DiffLine::Kind::Context, n, n, QString::fromUtf8(line).remove(QLatin1Char('\r'))});
        }
    }
    return collect.lines;
}

} // namespace coeus
