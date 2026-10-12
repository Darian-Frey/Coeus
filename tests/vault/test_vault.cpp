#include "vault/Vault.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using coeus::Vault;

class TestVault : public QObject {
    Q_OBJECT

private slots:
    void opensExistingFolder()
    {
        QTemporaryDir dir;
        const auto vault = Vault::open(dir.path());
        QVERIFY(vault.has_value());
        QCOMPARE(vault->rootPath(), QFileInfo(dir.path()).canonicalFilePath());
    }

    void rejectsMissingFolder()
    {
        QTemporaryDir dir;
        QVERIFY(!Vault::open(dir.filePath(QStringLiteral("absent"))).has_value());
    }

    void rejectsFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("note.md"));
        writeFile(path, "x");
        QVERIFY(!Vault::open(path).has_value());
    }

    void readsNoteAsUtf8()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("note.md"));
        writeFile(path, "# Κοῖος\n");
        const auto vault = Vault::open(dir.path());
        const auto text = vault->readNote(path);
        QVERIFY(text.has_value());
        QCOMPARE(*text, QStringLiteral("# Κοῖος\n"));
        QCOMPARE(vault->relativePath(path), QStringLiteral("note.md"));
    }

    void refusesPathsOutsideVault()
    {
        QTemporaryDir outer;
        QDir(outer.path()).mkdir(QStringLiteral("vault"));
        const QString outside = outer.filePath(QStringLiteral("secret.md"));
        writeFile(outside, "x");
        const auto vault = Vault::open(outer.filePath(QStringLiteral("vault")));
        QVERIFY(!vault->contains(outside));
        QVERIFY(!vault->readNote(outer.filePath(QStringLiteral("vault/../secret.md"))).has_value());
    }

    void classifiesNotes()
    {
        QVERIFY(Vault::isNote(QStringLiteral("a/b.md")));
        QVERIFY(Vault::isNote(QStringLiteral("README.MD")));
        QVERIFY(!Vault::isNote(QStringLiteral("image.png")));
        QVERIFY(Vault::nameFilters().contains(QStringLiteral("*.md")));
        QVERIFY(Vault::nameFilters().contains(QStringLiteral("*.pdf")));
    }

private:
    static void writeFile(const QString& path, const QByteArray& content)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(content);
    }
};

QTEST_GUILESS_MAIN(TestVault)
#include "test_vault.moc"
