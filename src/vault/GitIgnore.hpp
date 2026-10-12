#pragma once

#include <QHash>
#include <QString>

#include <map>
#include <memory>

struct git_repository;

namespace coeus {

// Git's ignore rules for one root, through libgit2 (nested .gitignore files,
// negation, .git/info/exclude and the user's global excludes). Two kinds of
// repository apply:
//  - the one containing the root, found by searching upwards;
//  - repositories nested inside the root (a vault of project folders), found
//    the first time a folder holding .git is checked. Callers check folders
//    before their contents, as tree walks do.
// A path is judged by the innermost repository containing it. Outside every
// repository nothing is ignored. Rules are read once; create a new instance
// after a .gitignore changes. Use from one thread only.
class GitIgnore {
public:
    explicit GitIgnore(const QString& rootPath);
    ~GitIgnore();
    GitIgnore(const GitIgnore&) = delete;
    GitIgnore& operator=(const GitIgnore&) = delete;

    // True if the root itself lies inside a repository.
    bool inRepository() const { return m_outer != nullptr; }
    int nestedRepositories() const { return int(m_nested.size()); }

    // `relativePath` is relative to the root; folders are matched as folders.
    bool isIgnored(const QString& relativePath, bool isDir);

private:
    struct RepoCloser {
        void operator()(git_repository* repo) const;
    };
    using Repo = std::unique_ptr<git_repository, RepoCloser>;

    static Repo openRepository(const QString& path, bool searchUpwards);
    static bool ignoredBy(git_repository* repo, const QString& pathInRepo, bool isDir);

    QString m_rootPath;
    Repo m_outer;
    QString m_outerPrefix;          // root's path inside m_outer: "" or ending in '/'
    std::map<QString, Repo> m_nested; // root-relative folder -> repository rooted there
    QHash<QString, bool> m_checked; // folders already probed for .git
};

} // namespace coeus
