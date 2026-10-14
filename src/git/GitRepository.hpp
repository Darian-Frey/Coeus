#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

#include <expected>
#include <memory>
#include <optional>

struct git_repository;

namespace coeus {

// One commit in a note's history (F-020).
struct GitCommit {
    QString id;      // full hex
    QString shortId; // first seven
    QString summary; // first line of the message
    QString message;
    QString author;
    QString email;
    QDateTime time;  // author time, in the author's offset
    QString path;    // the note's workdir-relative path in this commit (renames followed)
};

// One line of a diff (F-021): unchanged, or only in the old or new text.
struct DiffLine {
    enum class Kind { Context, Removed, Added };
    Kind kind = Kind::Context;
    int oldLine = 0; // 1-based in the old text; 0 for an added line
    int newLine = 0; // 1-based in the new text; 0 for a removed line
    QString text;    // without its line ending
};

// Who last changed one line of a note (F-021). An empty `id` means the line
// is not committed: new in the working copy or the unsaved buffer.
struct BlameLine {
    QString id;
    QString shortId;
    QString summary;
    QString author;
    QDateTime time;
};

// One changed path in the working tree or index (F-022), as git status sees it.
struct GitChange {
    enum class Kind { Added, Modified, Deleted, Renamed, TypeChanged, Untracked, Conflicted };
    QString path;     // workdir-relative
    Kind kind = Kind::Modified;
    bool staged = false;   // the index differs from HEAD for it
    bool unstaged = false; // the working tree differs from the index for it
    // "M", "A", "D", "R", "T", "?", "U"
    QString letter() const;
};

// A thin wrapper over a libgit2 repository (ARCHITECTURE.md git/). Not
// thread-safe: open one per worker task; every call blocks, so none may run
// on the UI thread (AV-005). Never writes to the working tree: commit()
// writes only the index and objects under .git/.
class GitRepository {
public:
    // The non-bare repository whose working tree contains `path`, searching upward.
    static std::expected<GitRepository, QString> discover(const QString& path);

    GitRepository(GitRepository&&) noexcept = default;
    GitRepository& operator=(GitRepository&&) noexcept = default;
    ~GitRepository();

    // Absolute, canonical, without a trailing slash.
    QString workdir() const { return m_workdir; }
    // `absolutePath` relative to the working tree, with forward slashes.
    QString relativePath(const QString& absolutePath) const;
    // "main"; "detached at a41c9e2"; or the branch HEAD names before the first commit.
    QString branch() const;

    // The commits that changed the file, newest first, up to `limit`: those
    // whose version of it differs from every parent's (a merge that took one
    // side unchanged is skipped, as git log does). A rename found by
    // similarity is followed to the old path. Empty before the first commit
    // or for an untracked file.
    std::expected<QList<GitCommit>, QString> fileHistory(const QString& relativePath, int limit = 500) const;
    // The file's bytes in `commitId` (full or abbreviated, or any revision).
    std::expected<QByteArray, QString> fileAt(const QString& commitId, const QString& relativePath) const;

    // Blame for `buffer`, the note's current text (saved or not), as of HEAD:
    // one entry per line of `buffer` (a final line without a newline counts).
    // Lines HEAD does not have, and every line of a file HEAD lacks, are not committed.
    std::expected<QList<BlameLine>, QString> blame(const QString& relativePath, const QByteArray& buffer) const;

    // Every line of `newText` and the lines of `oldText` it lost, in reading
    // order: the whole file, so a diff reads in context. Needs no repository.
    static std::expected<QList<DiffLine>, QString> diff(const QByteArray& oldText, const QByteArray& newText);

    // Every changed path, untracked ones included, ignored ones not, by path.
    // Coeus's own files (`.coeus/`, `.*.coeus-tmp-*`) are left out even when no
    // .gitignore excludes them, so they are never offered for a commit.
    std::expected<QList<GitChange>, QString> status() const;

    // Commits exactly `paths` as they are in the working tree (a missing file
    // as its deletion) on top of HEAD, with `message` and the identity Git's
    // config gives (user.name, user.email), and moves the branch. Anything
    // else already staged stays staged and out of the commit, as
    // `git commit -- <paths>` does. Returns the new commit.
    std::expected<GitCommit, QString> commit(const QStringList& paths, const QString& message);

    // How far the current branch is ahead of and behind its upstream, as of
    // the last fetch (the remote-tracking ref; nothing is fetched here).
    struct Tracking {
        QString upstream; // e.g. "origin/main"
        int ahead = 0;
        int behind = 0;
    };
    // nullopt when the branch has no upstream, HEAD is detached, or there are no commits.
    std::optional<Tracking> tracking() const;

private:
    struct Free {
        void operator()(git_repository* repo) const;
    };
    GitRepository(git_repository* repo, QString workdir);

    std::unique_ptr<git_repository, Free> m_repo;
    QString m_workdir;
};

} // namespace coeus
