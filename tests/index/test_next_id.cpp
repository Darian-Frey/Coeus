// SPEC.md §ID allocation, AV-003, AV-012: NextId reads the register files on
// disk, counts reservations and unsaved text, and never hands out the same
// number twice, even to allocations racing from several threads.

#include "IndexDump.hpp"

#include "index/Indexer.hpp"
#include "index/IndexStore.hpp"
#include "registers/NextId.hpp"
#include "registers/RegisterTemplates.hpp"

#include <QSet>
#include <QTemporaryDir>
#include <QTest>
#include <QtConcurrent/QtConcurrentRun>

using namespace coeus;
using coeus::testing::writeFile;

class TestNextId : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_vault;
    QString m_index;

    NextId::Request request(const QString& kind, const QString& project = QStringLiteral("proj"))
    {
        return {m_index, m_vault.path(), project, kind, QStringLiteral("test"), {}};
    }

private slots:
    void initTestCase()
    {
        writeFile(m_vault.filePath(QStringLiteral("proj/README.md")),
                  "> **Status:** Active\n> **Provenance:** T\n> **Last reviewed:** 2026-10-13\n> **Why this status:** T\n");
        writeFile(m_vault.filePath(QStringLiteral("proj/DECISIONS.md")), "### D-001 One\n**Status:** Accepted\n\n### D-004 Four\n");
        writeFile(m_vault.filePath(QStringLiteral("proj/docs/ATTACK_VECTORS.md")), "### AV-002 Two\n");
        m_index = m_vault.filePath(QStringLiteral(".coeus-index.db"));
        auto store = IndexStore::open(m_index);
        QVERIFY(Indexer(*store).scan({0, QStringLiteral("vault"), m_vault.path()}).has_value());
    }

    void nextAfterTheHighestAndReserved()
    {
        auto first = NextId::allocate(request(QStringLiteral("D")));
        QVERIFY2(first.has_value(), qPrintable(first.error()));
        QCOMPARE(first->ident, QStringLiteral("D-005"));
        QCOMPARE(first->registerFile, m_vault.filePath(QStringLiteral("proj/DECISIONS.md")));
        QVERIFY(first->fileExists);
        // Reserved: the next call moves on.
        QCOMPARE(NextId::allocate(request(QStringLiteral("D")))->ident, QStringLiteral("D-006"));
    }

    void readsTheFileOnDiskNotTheIndex()
    {
        // Written after the scan: the index has not seen D-040, the file has.
        writeFile(m_vault.filePath(QStringLiteral("proj/DECISIONS.md")), "### D-001 One\n\n### D-040 Late\n");
        QCOMPARE(NextId::allocate(request(QStringLiteral("D")))->ident, QStringLiteral("D-041"));
    }

    void countsUnsavedTextAndNestedRegisterFiles()
    {
        NextId::Request r = request(QStringLiteral("AV"));
        r.unsavedText = QStringLiteral("### AV-009 Typed but not saved\n");
        const auto av = NextId::allocate(r);
        QCOMPARE(av->ident, QStringLiteral("AV-010"));
        QCOMPARE(av->registerFile, m_vault.filePath(QStringLiteral("proj/docs/ATTACK_VECTORS.md")));
    }

    void missingRegisterFileStartsAtOne()
    {
        const auto imp = NextId::allocate(request(QStringLiteral("IMP")));
        QCOMPARE(imp->ident, QStringLiteral("IMP-001"));
        QVERIFY(!imp->fileExists);
        QCOMPARE(imp->registerFile, m_vault.filePath(QStringLiteral("proj/IMPROVEMENTS.md")));
    }

    void concurrentAllocationsNeverCollide()
    {
        // AV-012: eight threads at once, each its own connection.
        QList<QFuture<std::expected<NextId::Allocation, QString>>> futures;
        for (int i = 0; i < 8; ++i)
            futures.append(QtConcurrent::run([this] { return NextId::allocate(request(QStringLiteral("BUG"))); }));
        QSet<QString> idents;
        for (auto& future : futures) {
            const auto result = future.result();
            QVERIFY2(result.has_value(), qPrintable(result.error()));
            idents.insert(result->ident);
        }
        QCOMPARE(idents.size(), 8);
        QVERIFY(idents.contains(QStringLiteral("BUG-001")) && idents.contains(QStringLiteral("BUG-008")));
    }

    void refusesWhatIsNotAProject()
    {
        QVERIFY(!NextId::allocate(request(QStringLiteral("D"), QStringLiteral("nowhere"))).has_value());
        QVERIFY(!NextId::allocate(request(QStringLiteral("X"))).has_value());
    }

    void templatesAndHighest()
    {
        QCOMPARE(RegisterTemplates::fileName(QStringLiteral("AV")), QStringLiteral("ATTACK_VECTORS.md"));
        const QString entry = RegisterTemplates::entry(QStringLiteral("BUG"), QStringLiteral("BUG-003"), QDate(2026, 10, 13));
        QVERIFY(entry.startsWith(QStringLiteral("### BUG-003: Title\n**Status:** open\n")));
        QVERIFY(entry.contains(QStringLiteral("**Found:** 2026-10-13")));
        // Every kind's entry parses back as an entry of that kind.
        for (const QString& kind : RegisterTemplates::kinds()) {
            const QString ident = kind + QStringLiteral("-007");
            QCOMPARE(NextId::highest(RegisterTemplates::fileSkeleton(kind) + RegisterTemplates::entry(kind, ident, QDate(2026, 10, 13)), kind), 7);
        }
    }
};

QTEST_GUILESS_MAIN(TestNextId)
#include "test_next_id.moc"
