// D-025: the author's declared property values in `Coeus properties.md`,
// matched to a note by folder, and Coeus's own only where they mean something.

#include "registers/Vocabulary.hpp"
#include "vault/PropertyVocabulary.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

using namespace coeus;

class TestPropertyVocabulary : public QObject {
    Q_OBJECT

private slots:
    void parsesListsFoldersAndProse()
    {
        const auto lists = PropertyVocabulary::parse(QStringLiteral(
            "# Coeus properties\n"
            "Anything written here is ignored.\n"
            "- not under a heading, ignored\n"
            "\n"
            "## status\n"
            "Folders: prose, `drafts/`\n"
            "The scenes' statuses, in order:\n"
            "- outline\n"
            "- `draft`\n"
            "* revised\n"
            "- draft\n"
            "\n"
            "```markdown\n"
            "## fenced\n"
            "- ignored\n"
            "```\n"
            "## `pov`\n"
            "+ Theo Hartmann\n"
            "### a level-3 heading ends the list\n"
            "- not pov\n"
            "## empty\n"
            "Folders: x\n"));
        QCOMPARE(lists.size(), 2); // `empty` has no values, `fenced` is code
        QCOMPARE(lists[0].key, QStringLiteral("status"));
        QCOMPARE(lists[0].folders, (QStringList{QStringLiteral("prose"), QStringLiteral("drafts")}));
        QCOMPARE(lists[0].values, (QStringList{QStringLiteral("outline"), QStringLiteral("draft"), QStringLiteral("revised")}));
        QCOMPARE(lists[0].line, 5);
        QCOMPARE(lists[1].key, QStringLiteral("pov"));
        QVERIFY(lists[1].folders.isEmpty());
        QCOMPARE(lists[1].values, QStringList{QStringLiteral("Theo Hartmann")});
        // The starter explains and declares nothing.
        QVERIFY(PropertyVocabulary::parse(QString::fromUtf8(PropertyVocabulary::starter())).isEmpty());
    }

    void theNearestFolderWins()
    {
        const auto lists = PropertyVocabulary::parse(QStringLiteral("## status\n- open\n- closed\n"
                                                                    "## status\nFolders: prose\n- draft\n- final\n"
                                                                    "## status\nFolders: prose/act1\n- blocked\n"
                                                                    "## status\nFolders: pontus/docs\n- written\n"));
        const auto at = [&lists](const char* path, bool inVault = true) {
            const auto allowed = PropertyVocabulary::allowed(lists, QStringLiteral("status"), QLatin1String(path), inVault);
            return allowed ? allowed->values : QStringList{QStringLiteral("<none>")};
        };
        QCOMPARE(at("notes/x.md"), (QStringList{QStringLiteral("open"), QStringLiteral("closed")}));
        QCOMPARE(at("prose/x.md"), (QStringList{QStringLiteral("draft"), QStringLiteral("final")}));
        QCOMPARE(at("prose/act1/x.md"), QStringList{QStringLiteral("blocked")});
        QCOMPARE(at("prosecco/x.md"), (QStringList{QStringLiteral("open"), QStringLiteral("closed")})); // a folder, not a prefix
        QCOMPARE(at("pontus/docs/x.md", false), QStringList{QStringLiteral("written")});             // a linked root
        QVERIFY(PropertyVocabulary::allowed(lists, QStringLiteral("status"), QStringLiteral("prose/x.md"), true)->declared);
        QVERIFY(!PropertyVocabulary::allowed(lists, QStringLiteral("other"), QStringLiteral("prose/x.md"), true));
    }

    void coeusOwnVocabulariesOnlyWhereTheyMeanSomething()
    {
        const QList<PropertyVocabulary::List> none;
        // status in frontmatter is the author's: a project's status is its README header.
        QVERIFY(!PropertyVocabulary::allowed(none, QStringLiteral("status"), QStringLiteral("projects/x/README.md"), true));
        // stage is an idea's, under the vault's ideas/ only.
        const auto idea = PropertyVocabulary::allowed(none, QStringLiteral("stage"), QStringLiteral("ideas/theia.md"), true);
        QVERIFY(idea);
        QCOMPARE(idea->values, Vocabulary::ideaStage());
        QVERIFY(!idea->declared);
        QVERIFY(!PropertyVocabulary::allowed(none, QStringLiteral("stage"), QStringLiteral("prose/x.md"), true));
        QVERIFY(!PropertyVocabulary::allowed(none, QStringLiteral("stage"), QStringLiteral("ideas/x.md"), false)); // a linked root named ideas
        // A declared stage overrides Coeus's own.
        const auto declared = PropertyVocabulary::parse(QStringLiteral("## stage\n- seed\n- sprout\n"));
        QCOMPARE(PropertyVocabulary::allowed(declared, QStringLiteral("stage"), QStringLiteral("ideas/x.md"), true)->values,
                 (QStringList{QStringLiteral("seed"), QStringLiteral("sprout")}));
    }

    void loadReadsTheNoteAgainWhenItChanges()
    {
        QTemporaryDir vault;
        QVERIFY(PropertyVocabulary::load(vault.path()).isEmpty()); // no note yet
        const auto write = [&vault](const QByteArray& content) {
            QFile file(PropertyVocabulary::filePath(vault.path()));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(content);
        };
        write("## status\n- a\n");
        QCOMPARE(PropertyVocabulary::load(vault.path()).front().values, QStringList{QStringLiteral("a")});
        QThread::msleep(20);
        write("## status\n- a\n- b\n");
        QCOMPARE(PropertyVocabulary::load(vault.path()).front().values, (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
        QCOMPARE(PropertyVocabulary::filePath(vault.path()), vault.path() + QStringLiteral("/Coeus properties.md"));
    }
};

QTEST_GUILESS_MAIN(TestPropertyVocabulary)
#include "test_property_vocabulary.moc"
