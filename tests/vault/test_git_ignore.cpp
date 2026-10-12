#include "vault/GitIgnore.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <git2.h>

using coeus::GitIgnore;

namespace {

void write(const QString& path, const QByteArray& content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write(content);
}

bool initRepository(const QString& path)
{
    git_libgit2_init();
    git_repository* repo = nullptr;
    const int rc = git_repository_init(&repo, QFile::encodeName(path).constData(), 0);
    git_repository_free(repo);
    git_libgit2_shutdown();
    return rc == 0;
}

} // namespace

class TestGitIgnore : public QObject {
    Q_OBJECT

private slots:
    void outsideARepositoryNothingIsIgnored()
    {
        QTemporaryDir dir;
        write(dir.filePath(QStringLiteral(".gitignore")), "*.md\n");
        GitIgnore ignore(dir.path());
        QVERIFY(!ignore.inRepository());
        QVERIFY(!ignore.isIgnored(QStringLiteral("note.md"), false));
    }

    void followsGitRules()
    {
        QTemporaryDir repo;
        QVERIFY(initRepository(repo.path()));
        write(repo.filePath(QStringLiteral(".gitignore")), "build/\n*.log\ndrafts/*\n!drafts/keep.md\n");
        write(repo.filePath(QStringLiteral("docs/.gitignore")), "private.md\n");

        GitIgnore ignore(repo.path());
        QVERIFY(ignore.inRepository());
        QVERIFY(ignore.isIgnored(QStringLiteral("build"), true));
        QVERIFY(ignore.isIgnored(QStringLiteral("build/_deps/raylib/README.md"), false));
        QVERIFY(!ignore.isIgnored(QStringLiteral("build.md"), false));
        QVERIFY(ignore.isIgnored(QStringLiteral("drafts/old.md"), false));
        QVERIFY(!ignore.isIgnored(QStringLiteral("drafts/keep.md"), false)); // negation
        QVERIFY(ignore.isIgnored(QStringLiteral("docs/private.md"), false)); // nested .gitignore
        QVERIFY(!ignore.isIgnored(QStringLiteral("docs/public.md"), false));
        QVERIFY(!ignore.isIgnored(QStringLiteral("README.md"), false));
    }

    void rootInsideARepository()
    {
        QTemporaryDir repo;
        QVERIFY(initRepository(repo.path()));
        write(repo.filePath(QStringLiteral(".gitignore")), "notes/scratch/\n");
        QVERIFY(QDir(repo.path()).mkpath(QStringLiteral("notes/scratch")));

        // The root is a subfolder; paths are relative to it, rules to the repo.
        GitIgnore ignore(repo.filePath(QStringLiteral("notes")));
        QVERIFY(ignore.inRepository());
        QVERIFY(ignore.isIgnored(QStringLiteral("scratch"), true));
        QVERIFY(!ignore.isIgnored(QStringLiteral("idea.md"), false));
    }

    void nestedRepositoriesApplyTheirOwnRules()
    {
        // A vault of project folders, each its own repository (~/Projects).
        QTemporaryDir projects;
        QVERIFY(initRepository(projects.filePath(QStringLiteral("aether"))));
        write(projects.filePath(QStringLiteral("aether/.gitignore")), "build/\n");
        QVERIFY(initRepository(projects.filePath(QStringLiteral("pontus"))));
        write(projects.filePath(QStringLiteral("pontus/.gitignore")), "out/\n");
        QVERIFY(QDir(projects.path()).mkpath(QStringLiteral("notes/build")));

        GitIgnore ignore(projects.path());
        QVERIFY(!ignore.inRepository());
        // Folders are met top-down, as in a tree walk.
        QVERIFY(!ignore.isIgnored(QStringLiteral("aether"), true));
        QVERIFY(!ignore.isIgnored(QStringLiteral("pontus"), true));
        QVERIFY(!ignore.isIgnored(QStringLiteral("notes"), true));
        QCOMPARE(ignore.nestedRepositories(), 2);

        QVERIFY(ignore.isIgnored(QStringLiteral("aether/build"), true));
        QVERIFY(!ignore.isIgnored(QStringLiteral("aether/README.md"), false));
        QVERIFY(ignore.isIgnored(QStringLiteral("pontus/out"), true));
        QVERIFY(!ignore.isIgnored(QStringLiteral("pontus/build"), true)); // aether's rule stays in aether
        QVERIFY(!ignore.isIgnored(QStringLiteral("notes/build"), true));  // outside every repository
    }

    void nestedRepositoryInsideAnIgnoredFolderIsNotOpened()
    {
        QTemporaryDir repo;
        QVERIFY(initRepository(repo.path()));
        write(repo.filePath(QStringLiteral(".gitignore")), "build/\n");
        QVERIFY(initRepository(repo.filePath(QStringLiteral("build/_deps/raylib"))));

        GitIgnore ignore(repo.path());
        QVERIFY(ignore.isIgnored(QStringLiteral("build"), true));
        QCOMPARE(ignore.nestedRepositories(), 0);
    }

    void repositoryIgnoringTheRootIsDisregarded()
    {
        // A dotfiles-style repository ignoring everything must not hide notes.
        QTemporaryDir home;
        QVERIFY(initRepository(home.path()));
        write(home.filePath(QStringLiteral(".gitignore")), "*\n");
        QVERIFY(QDir(home.path()).mkpath(QStringLiteral("vault")));

        GitIgnore ignore(home.filePath(QStringLiteral("vault")));
        QVERIFY(!ignore.inRepository());
        QVERIFY(!ignore.isIgnored(QStringLiteral("note.md"), false));
    }
};

QTEST_GUILESS_MAIN(TestGitIgnore)
#include "test_git_ignore.moc"
