#include "vault/Journal.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using coeus::Journal;

namespace {

void write(const QString& path, const QByteArray& content)
{
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write(content);
}

void setMtime(const QString& path, const QDateTime& time)
{
    QFile file(path);
    if (file.open(QIODevice::ReadWrite))
        file.setFileTime(time, QFileDevice::FileModificationTime);
}

} // namespace

class TestJournal : public QObject {
    Q_OBJECT

private slots:
    void roundTripsExactly()
    {
        const QString text = QStringLiteral("line one\n\npath: not a header\ncoeus-journal 1\n\nΚοῖος, no trailing newline");
        const QByteArray bytes = Journal::serialise(QStringLiteral("/v/a b.md"), QByteArray("\x01\xff", 2), text);
        const auto entry = Journal::parse(bytes);
        QVERIFY(entry.has_value());
        QCOMPARE(entry->notePath, QStringLiteral("/v/a b.md"));
        QCOMPARE(entry->baseHash, QByteArray("\x01\xff", 2));
        QCOMPARE(entry->text, text);
        QVERIFY(!Journal::parse("garbage").has_value());
    }

    void writeAndRemove()
    {
        QTemporaryDir vault;
        const QString note = vault.filePath(QStringLiteral("n.md"));
        QVERIFY(Journal::write(vault.path(), note, {}, QStringLiteral("draft")).has_value());
        QVERIFY(QFile::exists(Journal::fileFor(vault.path(), note)));
        QVERIFY(Journal::fileFor(vault.path(), note).startsWith(vault.path() + QStringLiteral("/.coeus/journal/")));
        QCOMPARE(Journal::entries(vault.path()).size(), 1);
        Journal::remove(vault.path(), note);
        QVERIFY(Journal::entries(vault.path()).isEmpty());
    }

    void recoverableRules()
    {
        QTemporaryDir vault;
        const QDateTime now = QDateTime::currentDateTime();
        const QString same = vault.filePath(QStringLiteral("same.md"));
        const QString differs = vault.filePath(QStringLiteral("differs.md"));
        const QString newer = vault.filePath(QStringLiteral("newer.md"));
        const QString gone = vault.filePath(QStringLiteral("gone.md"));
        write(same, "identical");
        write(differs, "old");
        write(newer, "saved later");
        QVERIFY(Journal::write(vault.path(), same, {}, QStringLiteral("identical")).has_value());
        QVERIFY(Journal::write(vault.path(), differs, {}, QStringLiteral("unsaved work")).has_value());
        QVERIFY(Journal::write(vault.path(), newer, {}, QStringLiteral("older work")).has_value());
        QVERIFY(Journal::write(vault.path(), gone, {}, QStringLiteral("orphan")).has_value());
        setMtime(differs, now.addSecs(-60));
        setMtime(newer, now.addSecs(60));

        const auto entries = Journal::recoverable(vault.path());
        QStringList paths;
        for (const auto& e : entries)
            paths.append(QFileInfo(e.notePath).fileName());
        paths.sort();
        QCOMPARE(paths, (QStringList{QStringLiteral("differs.md"), QStringLiteral("gone.md"), QStringLiteral("newer.md")}));
        for (const auto& e : entries)
            QCOMPARE(e.noteChangedSince, e.notePath == newer); // offered, but flagged (AV-002)
        // Matching the disk: nothing to recover, so the entry is cleared.
        QVERIFY(!QFile::exists(Journal::fileFor(vault.path(), same)));
    }
};

QTEST_GUILESS_MAIN(TestJournal)
#include "test_journal.moc"
