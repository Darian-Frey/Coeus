#include "vault/AtomicWriter.hpp"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTest>

#include <sys/stat.h>
#include <unistd.h>

using coeus::AtomicWriter;

namespace {

QByteArray readAll(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QStringList tempLeftovers(const QString& dir)
{
    return QDir(dir).entryList({QStringLiteral("*.coeus-tmp-*")}, QDir::Files | QDir::Hidden);
}

mode_t modeOf(const QString& path)
{
    struct stat info {};
    ::stat(QFile::encodeName(path).constData(), &info);
    return info.st_mode & 07777;
}

} // namespace

class TestAtomicWriter : public QObject {
    Q_OBJECT

private slots:
    void createsNewFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("new.md"));
        QVERIFY(AtomicWriter::write(path, "# New\n").has_value());
        QCOMPARE(readAll(path), QByteArray("# New\n"));
        QVERIFY(tempLeftovers(dir.path()).isEmpty());
    }

    void replacesExistingFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("note.md"));
        QVERIFY(AtomicWriter::write(path, "old content that is longer").has_value());
        QVERIFY(AtomicWriter::write(path, "new").has_value());
        QCOMPARE(readAll(path), QByteArray("new"));
        QVERIFY(tempLeftovers(dir.path()).isEmpty());
    }

    void writesEmptyFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("empty.md"));
        QVERIFY(AtomicWriter::write(path, "x").has_value());
        QVERIFY(AtomicWriter::write(path, "").has_value());
        QCOMPARE(QFileInfo(path).size(), 0);
    }

    void keepsMode()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("private.md"));
        QVERIFY(AtomicWriter::write(path, "a").has_value());
        QCOMPARE(::chmod(QFile::encodeName(path).constData(), 0640), 0);
        QVERIFY(AtomicWriter::write(path, "b").has_value());
        QCOMPARE(modeOf(path), mode_t(0640));
    }

    void keepsSymlink()
    {
        QTemporaryDir dir;
        const QString real = dir.filePath(QStringLiteral("real.md"));
        const QString link = dir.filePath(QStringLiteral("link.md"));
        QVERIFY(AtomicWriter::write(real, "one").has_value());
        QVERIFY(QFile::link(real, link));
        QVERIFY(AtomicWriter::write(link, "two").has_value());
        QVERIFY(QFileInfo(link).isSymLink());
        QCOMPARE(readAll(real), QByteArray("two"));
    }

    void failsWithoutFolder()
    {
        QTemporaryDir dir;
        const auto result = AtomicWriter::write(dir.filePath(QStringLiteral("absent/note.md")), "x");
        QVERIFY(!result.has_value());
        QVERIFY(!result.error().isEmpty());
    }

    void refusesDirectory()
    {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("folder")));
        QVERIFY(!AtomicWriter::write(dir.filePath(QStringLiteral("folder")), "x").has_value());
        QVERIFY(tempLeftovers(dir.path()).isEmpty());
    }

    void failsInReadOnlyFolderAndKeepsOriginal()
    {
        if (::geteuid() == 0)
            QSKIP("root ignores folder permissions");
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("note.md"));
        QVERIFY(AtomicWriter::write(path, "original").has_value());
        const QByteArray dirPath = QFile::encodeName(dir.path());
        QCOMPARE(::chmod(dirPath.constData(), 0555), 0);
        const auto result = AtomicWriter::write(path, "replacement");
        ::chmod(dirPath.constData(), 0755);
        QVERIFY(!result.has_value());
        QCOMPARE(readAll(path), QByteArray("original"));
    }

    // AV-002: killing the writer at any moment leaves the old or the new
    // content, never a truncated or mixed file.
    void survivesKillDuringWrite()
    {
        constexpr int kRounds = 40;
        constexpr qsizetype kBytes = 4 * 1024 * 1024;
        const QByteArray contentA(kBytes, 'a');
        const QByteArray contentB(kBytes, 'b');

        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("big.md"));
        QVERIFY(AtomicWriter::write(path, contentA).has_value());

        for (int round = 0; round < kRounds; ++round) {
            QProcess helper;
            helper.start(QStringLiteral(ATOMIC_WRITE_HELPER), {path, QString::number(kBytes)});
            QVERIFY(helper.waitForStarted());
            QVERIFY(helper.waitForReadyRead());
            QTest::qWait(int(QRandomGenerator::global()->bounded(1, 60)));
            helper.kill();
            QVERIFY(helper.waitForFinished());
            QCOMPARE(helper.exitStatus(), QProcess::CrashExit);

            const QByteArray now = readAll(path);
            QVERIFY2(now == contentA || now == contentB,
                     qPrintable(QStringLiteral("round %1: %2 bytes, neither old nor new")
                                    .arg(round).arg(now.size())));
        }
    }
};

QTEST_GUILESS_MAIN(TestAtomicWriter)
#include "test_atomic_writer.moc"
