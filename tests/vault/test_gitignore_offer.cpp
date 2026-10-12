#include "vault/GitIgnoreOffer.hpp"
#include "vault/Roots.hpp"
#include "vault/VaultConfig.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <git2.h>

using namespace coeus;

namespace {

void write(const QString& path, const QByteArray& content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write(content);
}

QByteArray read(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
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

const QStringList kBoth{QLatin1String(GitIgnoreOffer::kIndexPattern), QLatin1String(GitIgnoreOffer::kTempPattern)};

} // namespace

class TestGitIgnoreOffer : public QObject {
    Q_OBJECT

private slots:
    // --- VaultConfig -------------------------------------------------------

    void configAddsTableToEmptyOrMissingFile()
    {
        const auto result = VaultConfig::withString("", "vault", "gitignore_offer", QStringLiteral("declined"));
        QVERIFY(result.has_value());
        QCOMPARE(*result, QByteArray("[vault]\ngitignore_offer = \"declined\"\n"));
    }

    void configKeepsCommentsAndOtherTables()
    {
        const QByteArray before = "# My settings\n"
                                  "[[roots]]\nname = \"pontus\"  # the physics one\npath = \"~/src/pontus\"\n\n"
                                  "[registers]\nreview_threshold_days = 21\n";
        const auto after = VaultConfig::withString(before, "vault", "gitignore_offer", QStringLiteral("declined"));
        QVERIFY(after.has_value());
        QVERIFY(after->startsWith(before.trimmed()));
        QVERIFY(after->contains("# the physics one"));
        QVERIFY(after->endsWith("\n[vault]\ngitignore_offer = \"declined\"\n"));
    }

    void configInsertsIntoExistingTableOrReplacesKey()
    {
        const QByteArray withTable = "[vault]\nwatcher_debounce_ms = 300 # slower disk\n\n[editor]\nautosave_interval_s = 5\n";
        const auto inserted = VaultConfig::withString(withTable, "vault", "gitignore_offer", QStringLiteral("declined"));
        QCOMPARE(*inserted, QByteArray("[vault]\ngitignore_offer = \"declined\"\nwatcher_debounce_ms = 300 # slower disk\n\n"
                                       "[editor]\nautosave_interval_s = 5\n"));

        const auto replaced = VaultConfig::withString(*inserted, "vault", "gitignore_offer", QStringLiteral("ask"));
        QVERIFY(replaced->contains("gitignore_offer = \"ask\"\n"));
        QVERIFY(!replaced->contains("declined"));
    }

    void configEscapesValues()
    {
        const auto result = VaultConfig::withString("", "t", "k", QStringLiteral("a \"quoted\" \\ path"));
        QVERIFY(result.has_value());
    }

    void configRefusesMalformedFile()
    {
        QVERIFY(!VaultConfig::withString("[vault\nbroken", "vault", "k", QStringLiteral("v")).has_value());
    }

    void configReadsAndWritesOnDisk()
    {
        QTemporaryDir vault;
        QVERIFY(!VaultConfig::string(vault.path(), "vault", "gitignore_offer"));
        QVERIFY(VaultConfig::setString(vault.path(), "vault", "gitignore_offer", QStringLiteral("declined")).has_value());
        QCOMPARE(VaultConfig::string(vault.path(), "vault", "gitignore_offer"), QStringLiteral("declined"));
        QCOMPARE(Roots::load(vault.path()).problems, QStringList()); // still a valid config
    }

    // --- GitIgnoreOffer ----------------------------------------------------

    void noOfferOutsideARepository()
    {
        QTemporaryDir vault;
        QVERIFY(GitIgnoreOffer::missingPatterns(vault.path()).isEmpty());
    }

    void offerAndAccept()
    {
        QTemporaryDir vault;
        QVERIFY(initRepository(vault.path()));
        write(vault.filePath(QStringLiteral(".gitignore")), "build/"); // no trailing newline
        QCOMPARE(GitIgnoreOffer::missingPatterns(vault.path()), kBoth);

        QVERIFY(GitIgnoreOffer::accept(vault.path(), kBoth).has_value());
        QCOMPARE(read(vault.filePath(QStringLiteral(".gitignore"))),
                 QByteArray("build/\n\n") + GitIgnoreOffer::kComment + "\n.coeus/\n.*.coeus-tmp-*\n");
        QVERIFY(GitIgnoreOffer::missingPatterns(vault.path()).isEmpty());
    }

    void acceptCreatesGitignore()
    {
        QTemporaryDir vault;
        QVERIFY(initRepository(vault.path()));
        QVERIFY(GitIgnoreOffer::accept(vault.path(), kBoth).has_value());
        QVERIFY(read(vault.filePath(QStringLiteral(".gitignore"))).startsWith(GitIgnoreOffer::kComment));
        QVERIFY(GitIgnoreOffer::missingPatterns(vault.path()).isEmpty());
    }

    void offersOnlyWhatIsMissing()
    {
        QTemporaryDir vault;
        QVERIFY(initRepository(vault.path()));
        write(vault.filePath(QStringLiteral(".gitignore")), ".coeus/\n");
        QCOMPARE(GitIgnoreOffer::missingPatterns(vault.path()), QStringList{QLatin1String(GitIgnoreOffer::kTempPattern)});
    }

    void vaultInsideARepositoryGetsItsOwnGitignore()
    {
        QTemporaryDir repo;
        QVERIFY(initRepository(repo.path()));
        const QString vault = repo.filePath(QStringLiteral("notes"));
        QVERIFY(QDir().mkpath(vault));
        QCOMPARE(GitIgnoreOffer::missingPatterns(vault), kBoth);
        QVERIFY(GitIgnoreOffer::accept(vault, kBoth).has_value());
        QVERIFY(QFile::exists(vault + QStringLiteral("/.gitignore")));
        QVERIFY(!QFile::exists(repo.filePath(QStringLiteral(".gitignore"))));
        QVERIFY(GitIgnoreOffer::missingPatterns(vault).isEmpty());
    }

    void declineIsRemembered()
    {
        QTemporaryDir vault;
        QVERIFY(!GitIgnoreOffer::declined(vault.path()));
        QVERIFY(GitIgnoreOffer::decline(vault.path()).has_value());
        QVERIFY(GitIgnoreOffer::declined(vault.path()));
    }
};

QTEST_GUILESS_MAIN(TestGitIgnoreOffer)
#include "test_gitignore_offer.moc"
