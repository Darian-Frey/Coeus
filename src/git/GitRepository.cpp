#include "git/GitRepository.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <git2.h>

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

} // namespace coeus
