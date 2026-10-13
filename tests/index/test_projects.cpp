#include "IndexDump.hpp"

#include "index/Indexer.hpp"
#include "index/IndexStore.hpp"

#include <QTemporaryDir>
#include <QTest>

using namespace coeus;
using coeus::testing::writeFile;

namespace {

const QByteArray kHeader = "> **Status:** Active\n> **Provenance:** Test\n> **Last reviewed:** 2026-10-13\n"
                           "> **Why this status:** Testing.\n\n";

} // namespace

class TestProjects : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_vault;
    std::optional<IndexStore> m_store;
    std::unique_ptr<Indexer> m_indexer;
    RootInfo m_root;

    QStringList rows(const char* sql)
    {
        auto q = m_store->database().prepare(sql);
        QStringList out;
        while (q->step().value_or(false)) {
            QStringList fields;
            for (int c = 0; c < q->columnCount(); ++c)
                fields.append(q->isNull(c) ? QStringLiteral("∅") : q->text(c));
            out.append(fields.join(QLatin1Char('|')));
        }
        return out;
    }

private slots:
    void initTestCase()
    {
        writeFile(m_vault.filePath(QStringLiteral("projects/coeus/README.md")), kHeader + "# Coeus\n");
        writeFile(m_vault.filePath(QStringLiteral("projects/coeus/DECISIONS.md")),
                  "### D-001 First\n**Status:** Accepted\n**Related:** F-001, AV-009\n\n"
                  "### D-002 Second\n**Status:** Proposed\n**Related:** D-001\n");
        writeFile(m_vault.filePath(QStringLiteral("projects/coeus/FEATURES.md")), "### F-001 Shell\n**Status:** Complete\n");
        writeFile(m_vault.filePath(QStringLiteral("projects/coeus/docs/BUGS.md")), "### BUG-001: Nested\n**Status:** open\n");
        writeFile(m_vault.filePath(QStringLiteral("projects/coeus/sub/README.md")), kHeader + "# A nested project\n");
        writeFile(m_vault.filePath(QStringLiteral("projects/coeus/sub/DECISIONS.md")),
                  "### D-001 Nested first\n**Status:** Accepted\n**Related:** F-001\n");
        writeFile(m_vault.filePath(QStringLiteral("projects/plain/README.md")), "# No header\n");
        writeFile(m_vault.filePath(QStringLiteral("projects/plain/DECISIONS.md")), "### D-001 Orphan\n**Status:** Accepted\n");
        writeFile(m_vault.filePath(QStringLiteral("notes.md")), "Not a register.\n");
        m_store.emplace(*IndexStore::open(QStringLiteral(":memory:")));
        m_indexer = std::make_unique<Indexer>(*m_store);
        m_root = {0, QStringLiteral("vault"), m_vault.path()};
        QVERIFY(m_indexer->scan(m_root).has_value());
    }

    void detectsProjectsFromStatusHeaders()
    {
        QCOMPARE(rows("SELECT path, status, last_reviewed FROM projects ORDER BY path"),
                 (QStringList{QStringLiteral("projects/coeus|Active|2026-10-13"), QStringLiteral("projects/coeus/sub|Active|2026-10-13")}));
    }

    void notesBelongToTheNearestProject()
    {
        QCOMPARE(rows("SELECT n.path, coalesce(p.path, '∅') FROM notes n LEFT JOIN projects p ON p.id = n.project_id ORDER BY n.path"),
                 (QStringList{QStringLiteral("notes.md|∅"),
                              QStringLiteral("projects/coeus/DECISIONS.md|projects/coeus"),
                              QStringLiteral("projects/coeus/FEATURES.md|projects/coeus"),
                              QStringLiteral("projects/coeus/README.md|projects/coeus"),
                              QStringLiteral("projects/coeus/docs/BUGS.md|projects/coeus"),
                              QStringLiteral("projects/coeus/sub/DECISIONS.md|projects/coeus/sub"),
                              QStringLiteral("projects/coeus/sub/README.md|projects/coeus/sub"),
                              QStringLiteral("projects/plain/DECISIONS.md|∅"),
                              QStringLiteral("projects/plain/README.md|∅")}));
    }

    void entriesBelongToTheirNotesProject()
    {
        QCOMPARE(rows("SELECT r.ident, coalesce(p.path, '∅'), r.status FROM registers r LEFT JOIN projects p ON p.id = r.project_id "
                      "ORDER BY p.path, r.ident"),
                 (QStringList{QStringLiteral("D-001|∅|Accepted"), // the orphan outside any project
                              QStringLiteral("BUG-001|projects/coeus|open"), QStringLiteral("D-001|projects/coeus|Accepted"),
                              QStringLiteral("D-002|projects/coeus|Proposed"), QStringLiteral("F-001|projects/coeus|Complete"),
                              QStringLiteral("D-001|projects/coeus/sub|Accepted")}));
    }

    void citationsResolveWithinTheProject()
    {
        QCOMPARE(rows("SELECT s.ident, sp.path, f.target_ident, coalesce(tn.path, '∅') FROM register_refs f "
                      "JOIN registers s ON s.id = f.src JOIN projects sp ON sp.id = s.project_id "
                      "LEFT JOIN registers t ON t.id = f.target LEFT JOIN notes tn ON tn.id = t.note ORDER BY sp.path, s.ident, f.target_ident"),
                 (QStringList{QStringLiteral("D-001|projects/coeus|AV-009|∅"), // no such entry: unresolved
                              QStringLiteral("D-001|projects/coeus|F-001|projects/coeus/FEATURES.md"),
                              QStringLiteral("D-002|projects/coeus|D-001|projects/coeus/DECISIONS.md"),
                              QStringLiteral("D-001|projects/coeus/sub|F-001|∅")})); // never across projects
    }

    void removingAHeaderRemovesTheProject()
    {
        writeFile(m_vault.filePath(QStringLiteral("projects/coeus/sub/README.md")), "# No longer a project\n");
        QVERIFY(m_indexer->update(m_root, QStringLiteral("projects/coeus/sub/README.md")).has_value());
        QCOMPARE(rows("SELECT path FROM projects"), QStringList{QStringLiteral("projects/coeus")});
        // Its notes and entries fall to the enclosing project.
        QCOMPARE(rows("SELECT p.path FROM registers r JOIN notes n ON n.id = r.note JOIN projects p ON p.id = r.project_id "
                      "WHERE n.path = 'projects/coeus/sub/DECISIONS.md'"),
                 QStringList{QStringLiteral("projects/coeus")});

        writeFile(m_vault.filePath(QStringLiteral("projects/coeus/sub/README.md")), kHeader + "# Back again\n");
        QVERIFY(m_indexer->update(m_root, QStringLiteral("projects/coeus/sub/README.md")).has_value());
        QCOMPARE(rows("SELECT count(*) FROM projects"), QStringList{QStringLiteral("2")});
    }
};

QTEST_GUILESS_MAIN(TestProjects)
#include "test_projects.moc"
