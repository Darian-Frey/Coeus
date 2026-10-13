#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

#include <expected>
#include <memory>

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

// A thin, read-only wrapper over a libgit2 repository (ARCHITECTURE.md
// git/). Not thread-safe: open one per worker task; every call blocks, so
// none may run on the UI thread (AV-005). Never writes to the working tree.
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

private:
    struct Free {
        void operator()(git_repository* repo) const;
    };
    GitRepository(git_repository* repo, QString workdir);

    std::unique_ptr<git_repository, Free> m_repo;
    QString m_workdir;
};

} // namespace coeus
