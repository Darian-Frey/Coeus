#include "vault/GitIgnore.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <git2.h>

namespace coeus {

void GitIgnore::RepoCloser::operator()(git_repository* repo) const
{
    git_repository_free(repo);
    git_libgit2_shutdown();
}

GitIgnore::Repo GitIgnore::openRepository(const QString& path, bool searchUpwards)
{
    git_libgit2_init();
    git_repository* repo = nullptr;
    const QByteArray encoded = QFile::encodeName(path);
    const unsigned flags = searchUpwards ? 0u : unsigned(GIT_REPOSITORY_OPEN_NO_SEARCH);
    if (git_repository_open_ext(&repo, encoded.constData(), flags, nullptr) != 0 || git_repository_is_bare(repo)) {
        git_repository_free(repo);
        git_libgit2_shutdown();
        return nullptr;
    }
    return Repo(repo);
}

bool GitIgnore::ignoredBy(git_repository* repo, const QString& pathInRepo, bool isDir)
{
    QByteArray path = pathInRepo.toUtf8();
    if (isDir && !path.endsWith('/'))
        path.append('/');
    int ignored = 0;
    return git_ignore_path_is_ignored(&ignored, repo, path.constData()) == 0 && ignored != 0;
}

GitIgnore::GitIgnore(const QString& rootPath)
    : m_rootPath(QFileInfo(rootPath).canonicalFilePath())
{
    m_outer = openRepository(rootPath, true);
    if (!m_outer)
        return;

    const QString workdir = QFileInfo(QString::fromUtf8(git_repository_workdir(m_outer.get()))).canonicalFilePath();
    const QString relative = QDir(workdir).relativeFilePath(m_rootPath);
    m_outerPrefix = relative == QLatin1String(".") ? QString() : relative + QLatin1Char('/');

    // A repository further up that ignores the root itself (a dotfiles repo
    // ignoring "*") must not hide every note: disregard it.
    if (!m_outerPrefix.isEmpty() && ignoredBy(m_outer.get(), m_outerPrefix, true)) {
        m_outer.reset();
        m_outerPrefix.clear();
    }
}

GitIgnore::~GitIgnore() = default;

bool GitIgnore::isIgnored(const QString& relativePath, bool isDir)
{
    // The innermost nested repository whose folder contains the path.
    QString owner;
    git_repository* repo = nullptr;
    for (const auto& [folder, nested] : m_nested) {
        if (relativePath.startsWith(folder + QLatin1Char('/')) && folder.size() > owner.size()) {
            owner = folder;
            repo = nested.get();
        }
    }

    bool ignored = false;
    if (repo)
        ignored = ignoredBy(repo, relativePath.mid(owner.size() + 1), isDir);
    else if (m_outer)
        ignored = ignoredBy(m_outer.get(), m_outerPrefix + relativePath, isDir);
    if (ignored || !isDir)
        return ignored;

    // A visible folder holding .git starts a nested repository.
    if (!m_checked.contains(relativePath)) {
        m_checked.insert(relativePath, true);
        const QString folder = m_rootPath + QLatin1Char('/') + relativePath;
        if (QFileInfo::exists(folder + QStringLiteral("/.git"))) {
            if (Repo nested = openRepository(folder, false))
                m_nested.emplace(relativePath, std::move(nested));
        }
    }
    return false;
}

} // namespace coeus
