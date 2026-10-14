#pragma once

// A scratch Git repository driven through libgit2, for the git/ tests:
// write files into the working tree, then commit exactly the paths named.

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>

#include <git2.h>

namespace coeus::testing {

class GitFixture {
public:
    explicit GitFixture(const QString& path)
        : m_path(path)
    {
        git_libgit2_init();
        git_repository_init_options options = GIT_REPOSITORY_INIT_OPTIONS_INIT;
        options.initial_head = "main";
        options.flags |= GIT_REPOSITORY_INIT_MKPATH;
        git_repository_init_ext(&m_repo, QFile::encodeName(path).constData(), &options);
    }
    ~GitFixture()
    {
        git_repository_free(m_repo);
        git_libgit2_shutdown();
    }
    GitFixture(const GitFixture&) = delete;
    GitFixture& operator=(const GitFixture&) = delete;

    bool valid() const { return m_repo != nullptr; }

    void write(const QString& relative, const QByteArray& content) const
    {
        const QString file = m_path + QLatin1Char('/') + relative;
        QDir().mkpath(QFileInfo(file).absolutePath());
        QFile out(file);
        if (out.open(QIODevice::WriteOnly))
            out.write(content);
    }
    void remove(const QString& relative) const { QFile::remove(m_path + QLatin1Char('/') + relative); }

    // Stages `paths` as they are on disk (removed if missing) and commits with
    // `parents` (default: HEAD's commit, if any). Moves HEAD's branch when
    // `moveHead`. Returns the new commit's hex id, empty on failure.
    QString commit(const QString& message, const QStringList& paths, qint64 time,
                   const QStringList& parents = {QStringLiteral("HEAD")}, bool moveHead = true)
    {
        git_index* index = nullptr;
        git_repository_index(&index, m_repo);
        for (const QString& path : paths) {
            if (QFileInfo::exists(m_path + QLatin1Char('/') + path))
                git_index_add_bypath(index, path.toUtf8().constData());
            else
                git_index_remove_bypath(index, path.toUtf8().constData());
        }
        git_index_write(index);
        git_oid treeId;
        git_index_write_tree(&treeId, index);
        git_index_free(index);
        git_tree* tree = nullptr;
        git_tree_lookup(&tree, m_repo, &treeId);

        QList<git_commit*> parentCommits;
        for (const QString& parent : parents) {
            git_object* object = nullptr;
            if (git_revparse_single(&object, m_repo, parent.toUtf8().constData()) == 0) {
                git_commit* commit = nullptr;
                git_commit_lookup(&commit, m_repo, git_object_id(object));
                parentCommits.append(commit);
                git_object_free(object);
            }
        }
        git_signature* signature = nullptr;
        git_signature_new(&signature, "Ada Lovelace", "ada@example.org", time, 60);
        git_oid id;
        const int rc = git_commit_create(&id, m_repo, moveHead ? "HEAD" : nullptr, signature, signature, nullptr,
                                         message.toUtf8().constData(), tree, size_t(parentCommits.size()),
                                         const_cast<const git_commit**>(parentCommits.data()));
        git_signature_free(signature);
        for (git_commit* parent : parentCommits)
            git_commit_free(parent);
        git_tree_free(tree);
        if (rc != 0)
            return {};
        char hex[GIT_OID_HEXSZ + 1] = {};
        git_oid_tostr(hex, sizeof hex, &id);
        return QString::fromLatin1(hex);
    }

    // Sets user.name and user.email in this repository's own config.
    void setIdentity(const char* name = "Grace Hopper", const char* email = "grace@example.org") const
    {
        git_config* config = nullptr;
        git_repository_config(&config, m_repo);
        git_config_set_string(config, "user.name", name);
        git_config_set_string(config, "user.email", email);
        git_config_free(config);
    }

    // Stages `paths` without committing (as `git add`).
    void stage(const QStringList& paths) const
    {
        git_index* index = nullptr;
        git_repository_index(&index, m_repo);
        for (const QString& path : paths)
            git_index_add_bypath(index, path.toUtf8().constData());
        git_index_write(index);
        git_index_free(index);
    }

    // Adds remote `origin` at `url` and makes it main's upstream, as `git push -u` would.
    void setUpstream(const QString& url) const
    {
        git_remote* remote = nullptr;
        git_remote_create(&remote, m_repo, "origin", url.toUtf8().constData());
        git_remote_free(remote);
        git_config* config = nullptr;
        git_repository_config(&config, m_repo);
        git_config_set_string(config, "branch.main.remote", "origin");
        git_config_set_string(config, "branch.main.merge", "refs/heads/main");
        git_config_free(config);
    }

    // A bare repository at `path`, to push to.
    static bool initBare(const QString& path)
    {
        git_libgit2_init();
        git_repository* repo = nullptr;
        const int rc = git_repository_init(&repo, QFile::encodeName(path).constData(), 1);
        git_repository_free(repo);
        git_libgit2_shutdown();
        return rc == 0;
    }

    // The id `revision` names in the repository at `path`, or empty.
    static QString resolve(const QString& path, const char* revision)
    {
        git_libgit2_init();
        git_repository* repo = nullptr;
        QString id;
        if (git_repository_open(&repo, QFile::encodeName(path).constData()) == 0) {
            git_object* object = nullptr;
            if (git_revparse_single(&object, repo, revision) == 0) {
                char hex[GIT_OID_HEXSZ + 1] = {};
                git_oid_tostr(hex, sizeof hex, git_object_id(object));
                id = QString::fromLatin1(hex);
                git_object_free(object);
            }
            git_repository_free(repo);
        }
        git_libgit2_shutdown();
        return id;
    }

    // Points HEAD at `id` directly (detached).
    bool detach(const QString& id) const
    {
        git_oid oid;
        return git_oid_fromstr(&oid, id.toLatin1().constData()) == 0 && git_repository_set_head_detached(m_repo, &oid) == 0;
    }

private:
    QString m_path;
    git_repository* m_repo = nullptr;
};

} // namespace coeus::testing
