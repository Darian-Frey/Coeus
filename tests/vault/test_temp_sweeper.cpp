#include "vault/AtomicWriter.hpp"
#include "vault/TempSweeper.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using coeus::TempSweeper;

namespace {

QString touch(const QString& path, const QDateTime& mtime)
{
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write("partial");
        file.flush(); // a later flush on close would reset the time
        file.setFileTime(mtime, QFileDevice::FileModificationTime);
    }
    return path;
}

} // namespace

class TestTempSweeper : public QObject {
    Q_OBJECT

private slots:
    void recognisesOnlyAtomicWriterNames()
    {
        QVERIFY(TempSweeper::isTempName(QStringLiteral(".note.md.coeus-tmp-1f3a9c")));
        QVERIFY(TempSweeper::isTempName(QStringLiteral("..hidden.md.coeus-tmp-0")));
        QVERIFY(!TempSweeper::isTempName(QStringLiteral("note.md.coeus-tmp-1f3a9c"))); // no leading dot
        QVERIFY(!TempSweeper::isTempName(QStringLiteral(".note.md.coeus-tmp-")));       // no suffix
        QVERIFY(!TempSweeper::isTempName(QStringLiteral(".note.md.coeus-tmp-xyz")));    // not hex
        QVERIFY(!TempSweeper::isTempName(QStringLiteral(".coeus-tmp-1f")));             // no basename
        QVERIFY(!TempSweeper::isTempName(QStringLiteral("note.md")));
    }

    void removesOnlyStaleTempFiles()
    {
        QTemporaryDir vault;
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const QDateTime old = now.addSecs(-120);
        QVERIFY(QDir(vault.path()).mkpath(QStringLiteral("projects/coeus")));
        QVERIFY(QDir(vault.path()).mkpath(QStringLiteral(".git/objects")));

        const QString staleTop = touch(vault.filePath(QStringLiteral(".a.md.coeus-tmp-aa")), old);
        const QString staleNested = touch(vault.filePath(QStringLiteral("projects/coeus/.b.md.coeus-tmp-bb")), old);
        const QString fresh = touch(vault.filePath(QStringLiteral(".c.md.coeus-tmp-cc")), now);
        const QString note = touch(vault.filePath(QStringLiteral("d.md")), old);
        const QString lookalike = touch(vault.filePath(QStringLiteral("e.md.coeus-tmp-ee")), old);
        const QString inGit = touch(vault.filePath(QStringLiteral(".git/objects/.f.coeus-tmp-ff")), old);

        const TempSweeper::Result result = TempSweeper::sweep(vault.path(), now);

        QCOMPARE(result.removed, 2);
        QVERIFY(result.failures.isEmpty());
        QVERIFY(!QFile::exists(staleTop));
        QVERIFY(!QFile::exists(staleNested));
        QVERIFY(QFile::exists(fresh));
        QVERIFY(QFile::exists(note));
        QVERIFY(QFile::exists(lookalike));
        QVERIFY(QFile::exists(inGit));
    }

    void leavesSymlinksAlone()
    {
        QTemporaryDir vault;
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const QString target = touch(vault.filePath(QStringLiteral("target.md")), now.addSecs(-120));
        const QString link = vault.filePath(QStringLiteral(".x.md.coeus-tmp-12"));
        QVERIFY(QFile::link(target, link));

        QCOMPARE(TempSweeper::sweep(vault.path(), now).removed, 0);
        QVERIFY(QFileInfo(link).isSymLink());
        QVERIFY(QFile::exists(target));
    }

    void sweepsWhatAnInterruptedWriteLeaves()
    {
        // A real AtomicWriter temp name must be recognised.
        QTemporaryDir vault;
        const QString note = vault.filePath(QStringLiteral("note.md"));
        QVERIFY(coeus::AtomicWriter::write(note, "x").has_value());
        const QString leftover = touch(
            vault.filePath(QStringLiteral(".note.md%1%2").arg(QLatin1String(coeus::AtomicWriter::kTempMarker),
                                                              QStringLiteral("9e3779b97f4a7c15"))),
            QDateTime::currentDateTimeUtc().addSecs(-3600));
        QCOMPARE(TempSweeper::sweep(vault.path()).removed, 1);
        QVERIFY(!QFile::exists(leftover));
        QVERIFY(QFile::exists(note));
    }

    void missingRootIsHarmless()
    {
        QTemporaryDir dir;
        QCOMPARE(TempSweeper::sweep(dir.filePath(QStringLiteral("absent"))).removed, 0);
    }
};

QTEST_GUILESS_MAIN(TestTempSweeper)
#include "test_temp_sweeper.moc"
