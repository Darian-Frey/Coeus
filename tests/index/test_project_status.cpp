// F-032: build status from ROADMAP and CHANGELOG, and ideas in the index.

#include "IndexDump.hpp"

#include "index/IndexQueries.hpp"
#include "index/IndexStore.hpp"
#include "index/Indexer.hpp"
#include "registers/ProjectStatus.hpp"

#include <QTemporaryDir>
#include <QTest>

using namespace coeus;
using coeus::testing::writeFile;

class TestProjectStatus : public QObject {
    Q_OBJECT

private slots:
    void currentPhaseIsTheFirstNotComplete()
    {
        const auto build = ProjectStatus::parse(QStringLiteral(
            "# Roadmap\n\n"
            "## Phase 0 — Scaffold\n**Status:** Complete (2026-10-01)\n\n"
            "## Phase 1 — Core\n**Goal:** x\n**Status:** Complete 2026-10-05\n\n"
            "```\n## Phase 9 — Inside a fence\n```\n"
            "## Phase 2 — Playlist\n**Status:** Feature-complete 2026-09-02; clauses unverified\n\n"
            "## Phase 3 — Polish\n**Status:** Not started\n\n"
            "## Later / unscheduled\n**Status:** whatever\n"), std::nullopt);
        QVERIFY(build.hasRoadmap);
        QCOMPARE(build.phases, 4);
        QCOMPARE(build.phasesComplete, 2);
        QCOMPARE(build.phase, QStringLiteral("Phase 2"));
        QCOMPARE(build.phaseTitle, QStringLiteral("Playlist"));
        QCOMPARE(build.phaseStatus, QStringLiteral("Feature-complete 2026-09-02; clauses unverified"));
        QCOMPARE(build.summary(), QStringLiteral("Phase 2 — Playlist · Feature-complete 2026-09-02; clauses unverified (2 of 4 complete)"));
        QVERIFY(!build.hasChangelog);
    }

    void allCompleteOrNoStatus()
    {
        const auto done = ProjectStatus::parse(QStringLiteral("## Phase 1 — A\n**Status:** Complete\n## Phase 2 — B\n**Status:** Complete\n"), {});
        QCOMPARE(done.summary(), QStringLiteral("All 2 phases complete"));
        const auto bare = ProjectStatus::parse(QStringLiteral("## Phase 0 — Groundwork: prove the pipe\nNo status line.\n"), {});
        QCOMPARE(bare.summary(), QStringLiteral("Phase 0 — Groundwork: prove the pipe · no status (0 of 1 complete)"));
        QCOMPARE(ProjectStatus::parse(QStringLiteral("# Roadmap\nNothing yet.\n"), {}).summary(), QString());
    }

    void unreleasedChangesAndTheLastRelease()
    {
        const auto build = ProjectStatus::parse(std::nullopt, QStringLiteral(
            "# Changelog\n\n## [Unreleased]\n### Added\n- One\n- Two\n  continued\n### Fixed\n* Three\n\n"
            "## [1.0.0] - 2026-09-10\n### Added\n- Old\n\n## [0.9.0] - 2026-08-01\n- Older\n"));
        QVERIFY(build.hasChangelog);
        QCOMPARE(build.unreleased, 3);
        QCOMPARE(build.lastRelease, QStringLiteral("1.0.0"));
        QCOMPARE(build.lastReleaseDate, QStringLiteral("2026-09-10"));
        const auto fresh = ProjectStatus::parse(std::nullopt, QStringLiteral("## [Unreleased]\n- Only\n"));
        QCOMPARE(fresh.unreleased, 1);
        QVERIFY(fresh.lastRelease.isEmpty());
    }

    void documentsAreFoundInDocsFolders()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath(QStringLiteral("a/ROADMAP.md")), "x");
        writeFile(dir.filePath(QStringLiteral("a/docs/ROADMAP.md")), "shadowed");
        writeFile(dir.filePath(QStringLiteral("b/Docs/CHANGELOG.md")), "x");
        QCOMPARE(ProjectStatus::find(dir.filePath(QStringLiteral("a")), QStringLiteral("ROADMAP.md")), dir.filePath(QStringLiteral("a/ROADMAP.md")));
        QCOMPARE(ProjectStatus::find(dir.filePath(QStringLiteral("b")), QStringLiteral("CHANGELOG.md")), dir.filePath(QStringLiteral("b/Docs/CHANGELOG.md")));
        QVERIFY(!ProjectStatus::find(dir.filePath(QStringLiteral("b")), QStringLiteral("ROADMAP.md")));
    }

    void ideasAreNotesUnderIdeasWithAStage()
    {
        QTemporaryDir vault;
        writeFile(vault.filePath(QStringLiteral("ideas/theia.md")),
                  "---\nstage: scoped\ncodename: Theia\nrepo: https://example.org/theia\nsummary: A lamp\n---\n# Theia\n");
        writeFile(vault.filePath(QStringLiteral("ideas/deep/hermes.md")), "---\nstage: idea\n---\n# Hermes\n");
        writeFile(vault.filePath(QStringLiteral("ideas/no stage.md")), "---\ntags: [idea]\n---\n# Not one\n");
        writeFile(vault.filePath(QStringLiteral("elsewhere.md")), "---\nstage: idea\n---\n# Outside ideas/\n");
        const QString index = vault.filePath(QStringLiteral("index.db"));
        auto store = IndexStore::open(index);
        const RootInfo root{0, QStringLiteral("vault"), vault.path()};
        QVERIFY(store->putRoot(root));
        QVERIFY(Indexer(*store).scan(root));
        const auto ideas = IndexQueries::ideas(index);
        QVERIFY(ideas);
        QCOMPARE(ideas->size(), 2);
        QCOMPARE(ideas->at(0).notePath, vault.filePath(QStringLiteral("ideas/deep/hermes.md")));
        QCOMPARE(ideas->at(0).stage, QStringLiteral("idea"));
        QVERIFY(ideas->at(0).codename.isEmpty());
        QCOMPARE(ideas->at(1).codename, QStringLiteral("Theia"));
        QCOMPARE(ideas->at(1).repo, QStringLiteral("https://example.org/theia"));
        QCOMPARE(ideas->at(1).summary, QStringLiteral("A lamp"));
    }
};

QTEST_GUILESS_MAIN(TestProjectStatus)
#include "test_project_status.moc"
