// AV-001: an index kept up to date incrementally must equal one rebuilt from
// scratch. A fixture vault with a linked root is indexed, edited through a
// scripted sequence and then a seeded random one, with Indexer::update after
// each change; the result is compared row by row with a fresh full scan.

#include "IndexDump.hpp"

#include "index/Indexer.hpp"
#include "index/IndexStore.hpp"

#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTest>

using namespace coeus;
using coeus::testing::dumpIndex;
using coeus::testing::writeFile;

namespace {

QByteArray noteText(int n, QRandomGenerator& random)
{
    static const char* const kTargets[] = {
        "[[n0]]", "[[n1]]", "[[n7]]", "[[Hub]]", "[[folder/n3]]", "[[Alias One]]", "[[Missing]]",
        "[[linked:L0]]", "[Rel](../n2.md)", "[Same](n4.md)", "![[pic.png]]", "[[n5#Heading]]", "[[n6^blk]]",
    };
    QByteArray text;
    if (random.bounded(3) == 0)
        text += "---\naliases: [Alias " + QByteArray::number(random.bounded(3)) + "]\ntags: [t" +
                QByteArray::number(n % 4) + "]\nrank: " + QByteArray::number(n) + "\n---\n";
    text += "# Note " + QByteArray::number(n) + "\n\n";
    const int links = int(random.bounded(5));
    for (int i = 0; i < links; ++i)
        text += QByteArray(kTargets[random.bounded(int(std::size(kTargets)))]) + " #tag" + QByteArray::number(i) + "\n";
    text += "\n## Heading\n\nParagraph " + QByteArray::number(random.generate()) + " ^blk\n";
    return text;
}

const QByteArray kHeader = "> **Status:** Active\n> **Provenance:** Fixture\n> **Last reviewed:** 2026-10-13\n"
                           "> **Why this status:** Testing.\n\n# Project\n";

// A register file: entries cite each other, sometimes duplicating an ident.
QByteArray registerText(int seed, QRandomGenerator& random)
{
    QByteArray text = "# Register\n\n";
    const int count = 2 + int(random.bounded(4));
    for (int i = 1; i <= count; ++i) {
        const int number = (i == count && seed % 2) ? 1 : i; // seed odd: a duplicate D-001
        text += "### D-" + QByteArray::number(number).rightJustified(3, '0') + " Entry " + QByteArray::number(i) + "\n"
                "**Status:** Accepted\n**Related:** D-" + QByteArray::number(1 + random.bounded(count)).rightJustified(3, '0')
                + ", F-001\n\n**Context.** Seed " + QByteArray::number(seed) + ".\n\n";
    }
    return text;
}

} // namespace

class TestRebuildEquivalence : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_vault;
    QTemporaryDir m_linked;
    RootInfo m_root0;
    RootInfo m_root1;

    QStringList rebuildDump()
    {
        auto fresh = IndexStore::open(QStringLiteral(":memory:"));
        Indexer indexer(*fresh);
        indexer.scan(m_root1);
        indexer.scan(m_root0);
        return dumpIndex(fresh->database());
    }

    void compare(IndexStore& incremental, const char* stage)
    {
        const QStringList a = dumpIndex(incremental.database());
        const QStringList b = rebuildDump();
        if (a == b)
            return;
        QStringList onlyA, onlyB;
        for (const QString& row : a)
            if (!b.contains(row))
                onlyA.append(row);
        for (const QString& row : b)
            if (!a.contains(row))
                onlyB.append(row);
        QFAIL(qPrintable(QStringLiteral("%1: incremental and rebuilt differ\n  only incremental:\n    %2\n  only rebuilt:\n    %3")
                             .arg(QLatin1String(stage), onlyA.mid(0, 10).join(QStringLiteral("\n    ")),
                                  onlyB.mid(0, 10).join(QStringLiteral("\n    ")))));
    }

private slots:
    void initTestCase()
    {
        m_root0 = {0, QStringLiteral("vault"), m_vault.path()};
        m_root1 = {1, QStringLiteral("linked"), m_linked.path()};
    }

    void incrementalEqualsRebuild()
    {
        QRandomGenerator random(20261012); // fixed seed: failures reproduce
        for (int n = 0; n < 12; ++n)
            writeFile(m_vault.filePath(QStringLiteral("%1n%2.md").arg(n % 3 ? QString() : QStringLiteral("folder/")).arg(n)),
                      noteText(n, random));
        writeFile(m_vault.filePath(QStringLiteral("Hub.md")), "# Hub\n[[n1]] [[n2]]\n");
        writeFile(m_vault.filePath(QStringLiteral("folder/README.md")), kHeader);
        writeFile(m_vault.filePath(QStringLiteral("folder/DECISIONS.md")), registerText(1, random));
        writeFile(m_linked.filePath(QStringLiteral("L0.md")), "# Linked\n[[Hub]]\n");

        auto store = IndexStore::open(QStringLiteral(":memory:"));
        Indexer indexer(*store);
        QVERIFY(indexer.scan(m_root1).has_value());
        QVERIFY(indexer.scan(m_root0).has_value());
        compare(*store, "initial");

        auto edit = [&](const RootInfo& root, const QString& path, const QByteArray& text) {
            writeFile(root.path + QLatin1Char('/') + path, text);
            QVERIFY(indexer.update(root, path).has_value());
        };
        auto remove = [&](const RootInfo& root, const QString& path) {
            QFile::remove(root.path + QLatin1Char('/') + path);
            QVERIFY(indexer.update(root, path).has_value());
        };

        // Scripted: each case once.
        edit(m_root0, QStringLiteral("Missing.md"), "# Now exists\n"); // resolves links elsewhere
        compare(*store, "create resolves");
        remove(m_root0, QStringLiteral("Hub.md")); // unresolves links elsewhere
        compare(*store, "delete unresolves");
        edit(m_root0, QStringLiteral("deep/folder/n3.md"), "# Second n3\n"); // ambiguity appears
        compare(*store, "duplicate title");
        { // rename = delete + create, as editors that save by rename do
            const QString from = QStringLiteral("n1.md"), to = QStringLiteral("renamed/n1.md");
            QDir(m_vault.path()).mkpath(QStringLiteral("renamed"));
            QVERIFY(QFile::rename(m_vault.filePath(from), m_vault.filePath(to)));
            QVERIFY(indexer.update(m_root0, from).has_value());
            QVERIFY(indexer.update(m_root0, to).has_value());
        }
        compare(*store, "rename");
        { // touch: new mtime, same content
            QFile file(m_vault.filePath(QStringLiteral("n2.md")));
            QVERIFY(file.open(QIODevice::ReadWrite));
            QVERIFY(file.setFileTime(QDateTime::currentDateTime().addDays(-1), QFileDevice::FileModificationTime));
            file.close();
            QVERIFY(indexer.update(m_root0, QStringLiteral("n2.md")).has_value());
        }
        compare(*store, "touch");
        edit(m_root0, QStringLiteral("n4.md"), "---\naliases: [Alias One]\n---\n# Aliased\n");
        compare(*store, "alias");
        edit(m_root1, QStringLiteral("L0.md"), "# Linked changed\n[[n0]]\n");
        compare(*store, "linked root");
        edit(m_root0, QStringLiteral("broken.md"), "---\nbad: [\n---\n# Body survives #ok\n");
        compare(*store, "invalid frontmatter");
        edit(m_root0, QStringLiteral("folder/README.md"), "# No header any more\n"); // project vanishes
        compare(*store, "project removed");
        edit(m_root0, QStringLiteral("folder/README.md"), kHeader); // and returns
        compare(*store, "project restored");
        edit(m_root0, QStringLiteral("folder/deep/README.md"), kHeader); // a nested project
        edit(m_root0, QStringLiteral("folder/deep/FEATURES.md"), registerText(2, random));
        compare(*store, "nested project");

        // Random: create, rewrite or delete notes in both roots.
        for (int step = 0; step < 150; ++step) {
            const bool linked = random.bounded(5) == 0;
            const RootInfo& root = linked ? m_root1 : m_root0;
            const int n = int(random.bounded(16));
            const QString path = linked ? QStringLiteral("L%1.md").arg(n % 4)
                                        : QStringLiteral("%1n%2.md").arg(n % 3 ? QString() : QStringLiteral("folder/")).arg(n);
            if (random.bounded(4) == 0)
                remove(root, path);
            else
                edit(root, path, noteText(n, random));
            if (step % 3 == 0) { // register files change too
                // Two DECISIONS files in one project share idents, so which D-001 a
                // citation lands on must not depend on the order rows were written.
                static const QString kRegisterPaths[] = {QStringLiteral("folder/DECISIONS.md"),
                                                         QStringLiteral("folder/old/DECISIONS.md"),
                                                         QStringLiteral("folder/deep/FEATURES.md")};
                const QString registerPath = kRegisterPaths[random.bounded(3)];
                if (random.bounded(5) == 0)
                    remove(m_root0, registerPath);
                else
                    edit(m_root0, registerPath, registerText(int(random.bounded(5)), random));
            }
            if (step % 25 == 24)
                compare(*store, qPrintable(QStringLiteral("random step %1").arg(step)));
        }
        compare(*store, "final");

        QVERIFY(indexer.verify(m_root0)->clean());
        QVERIFY(indexer.verify(m_root1)->clean());
    }
};

QTEST_GUILESS_MAIN(TestRebuildEquivalence)
#include "test_rebuild_equivalence.moc"
