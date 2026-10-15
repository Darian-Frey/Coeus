#include "vault/DailyNotes.hpp"
#include "vault/NewNote.hpp"
#include "vault/Roots.hpp"
#include "vault/Templates.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace coeus;

namespace {

const QDateTime kNow(QDate(2026, 10, 13), QTime(9, 5));

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

} // namespace

class TestDailyTemplates : public QObject {
    Q_OBJECT

private slots:
    // --- Templates ---------------------------------------------------------

    void rendersVariables()
    {
        const auto r = Templates::render(QStringLiteral("{{title}} on {{date}} at {{time}}; {{ date:dddd d MMMM }}; "
                                                        "{{time:HH:mm:ss}}; {{next:F}}; {{unknown}}"),
                                         kNow, QStringLiteral("Meeting"));
        QCOMPARE(r.text, QStringLiteral("Meeting on 2026-10-13 at 09:05; Tuesday 13 October; 09:05:00; {{next:F}}; {{unknown}}"));
        QCOMPARE(r.unsupported, (QStringList{QStringLiteral("{{next:F}}"), QStringLiteral("{{unknown}}")}));
    }

    void emptyNoteTakesTheWholeTemplate()
    {
        int caret = -1;
        const QString t = QStringLiteral("---\ntags: [meeting]\n---\n# Agenda\n");
        QCOMPARE(Templates::insert(QStringLiteral("  \n"), 0, t, &caret), t);
        QCOMPARE(caret, int(t.size()));
    }

    void mergesFrontmatterWithoutOverwriting()
    {
        const QString note = QStringLiteral("---\nstatus: Active\n# my comment\n---\n# Note\n\nText.\n");
        const QString tmpl = QStringLiteral("---\nstatus: Dormant\ntags: [meeting, weekly]\nrank: 2\n---\n## Agenda\n- item\n");
        const int cursor = int(note.indexOf(QStringLiteral("Text.")));
        int caret = 0;
        const QString result = Templates::insert(note, cursor, tmpl, &caret);
        QCOMPARE(result, QStringLiteral("---\nstatus: Active\n# my comment\ntags: [meeting, weekly]\nrank: 2\n---\n"
                                        "# Note\n\n## Agenda\n- item\nText.\n"));
        QCOMPARE(result.mid(caret, 5), QStringLiteral("Text.")); // cursor after the inserted body
    }

    void templateWithoutFrontmatterIntoANoteWithout()
    {
        int caret = 0;
        QCOMPARE(Templates::insert(QStringLiteral("ab"), 1, QStringLiteral("X"), &caret), QStringLiteral("aXb"));
        QCOMPARE(caret, 2);
    }

    void listsTheTemplatesFolder()
    {
        QTemporaryDir vault;
        write(vault.filePath(QStringLiteral("templates/Meeting.md")), "x");
        write(vault.filePath(QStringLiteral("templates/sub/Weekly.md")), "x");
        write(vault.filePath(QStringLiteral("templates/notes.txt")), "x");
        QCOMPARE(Templates::list(vault.path()).size(), 2);

        write(Roots::configPath(vault.path()), "[templates]\nfolder = \"_tpl\"\n");
        write(vault.filePath(QStringLiteral("_tpl/Only.md")), "x");
        QCOMPARE(Templates::list(vault.path()), QStringList{vault.filePath(QStringLiteral("_tpl/Only.md"))});

        write(Roots::configPath(vault.path()), "[templates]\nfolder = \"../outside\"\n");
        QVERIFY(Templates::list(vault.path()).isEmpty());
    }

    // --- Daily notes -------------------------------------------------------

    void pathsFollowTheSettings()
    {
        DailyNotes::Settings s;
        QCOMPARE(DailyNotes::pathFor(QStringLiteral("/v"), kNow.date(), s).value(), QStringLiteral("/v/daily/2026-10-13.md"));
        s.folder = QStringLiteral("journal");
        s.format = QStringLiteral("yyyy/MM/yyyy-MM-dd");
        QCOMPARE(DailyNotes::pathFor(QStringLiteral("/v"), kNow.date(), s).value(),
                 QStringLiteral("/v/journal/2026/10/2026-10-13.md"));
        s.folder = QStringLiteral("../escape");
        QVERIFY(!DailyNotes::pathFor(QStringLiteral("/v"), kNow.date(), s).has_value());
    }

    void createsOnceAndNeverOverwrites()
    {
        QTemporaryDir dir;
        const Vault vault = *Vault::open(dir.path());
        const auto first = DailyNotes::openOrCreate(vault, kNow);
        QVERIFY2(first.has_value(), qPrintable(first.error()));
        QVERIFY(first->created);
        QCOMPARE(read(first->path), QByteArray("# 2026-10-13\n"));

        write(first->path, "my notes for the day\n");
        const auto second = DailyNotes::openOrCreate(vault, kNow);
        QVERIFY(!second->created);
        QCOMPARE(read(second->path), QByteArray("my notes for the day\n")); // untouched
    }

    void usesTheConfiguredTemplate()
    {
        QTemporaryDir dir;
        write(Roots::configPath(dir.path()),
              "[daily]\nfolder = \"journal\"\nformat = \"yyyy/yyyy-MM-dd\"\ntemplate = \"templates/Daily.md\"\n");
        write(dir.filePath(QStringLiteral("templates/Daily.md")), "---\ntags: [daily]\n---\n# {{title}}\n\nStarted {{time}}. {{next:D}}\n");
        const auto opened = DailyNotes::openOrCreate(*Vault::open(dir.path()), kNow);
        QVERIFY2(opened.has_value(), qPrintable(opened.error()));
        QCOMPARE(opened->path, dir.filePath(QStringLiteral("journal/2026/2026-10-13.md")));
        QCOMPARE(read(opened->path), QByteArray("---\ntags: [daily]\n---\n# 2026-10-13\n\nStarted 09:05. {{next:D}}\n"));
        QCOMPARE(opened->unsupported, QStringList{QStringLiteral("{{next:D}}")});
    }

    void missingTemplateIsAnErrorNotAnEmptyNote()
    {
        QTemporaryDir dir;
        write(Roots::configPath(dir.path()), "[daily]\ntemplate = \"templates/Gone.md\"\n");
        QVERIFY(!DailyNotes::openOrCreate(*Vault::open(dir.path()), kNow).has_value());
        QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("daily/2026-10-13.md"))));
    }

    void templatesNameTheirFolder()
    {
        const QString meeting = QStringLiteral("---\ncoeus-folder: meetings/2026/\ntags: [meeting]\n---\n# {{title}}\n");
        QCOMPARE(Templates::placement(meeting), QStringLiteral("meetings/2026"));
        QCOMPARE(Templates::placement(QStringLiteral("# No frontmatter\n")), QString());
        QCOMPARE(Templates::placement(QStringLiteral("---\ntags: [x]\n---\nbody\n")), QString());
        // Never copied into a note: other keys stay, a block left empty goes.
        QCOMPARE(Templates::withoutPlacement(meeting), QStringLiteral("---\ntags: [meeting]\n---\n# {{title}}\n"));
        QCOMPARE(Templates::withoutPlacement(QStringLiteral("---\ncoeus-folder: x\n---\nbody\n")), QStringLiteral("body\n"));
        QCOMPARE(Templates::render(meeting, QDateTime(QDate(2026, 10, 15), QTime(9, 0)), QStringLiteral("Standup")).text,
                 QStringLiteral("---\ntags: [meeting]\n---\n# Standup\n"));
    }

    void newNotePathsAreChecked()
    {
        QTemporaryDir root;
        write(root.filePath(QStringLiteral("notes/Existing.md")), "x");
        QCOMPARE(NewNote::pathFor(root.path(), QStringLiteral("notes"), QStringLiteral(" Plan ")).value(), root.filePath(QStringLiteral("notes/Plan.md")));
        QCOMPARE(NewNote::pathFor(root.path(), QString(), QStringLiteral("Plan.md")).value(), root.filePath(QStringLiteral("Plan.md")));
        QCOMPARE(NewNote::pathFor(root.path(), QStringLiteral("new/deeper/"), QStringLiteral("Plan")).value(),
                 root.filePath(QStringLiteral("new/deeper/Plan.md"))); // folders not made yet are fine
        QVERIFY(NewNote::pathFor(root.path(), QStringLiteral("notes"), QStringLiteral("Existing")).error().contains(QLatin1String("already exists")));
        QVERIFY(!NewNote::pathFor(root.path(), QString(), QStringLiteral("  ")));
        QVERIFY(!NewNote::pathFor(root.path(), QString(), QStringLiteral("a/b")));
        QVERIFY(!NewNote::pathFor(root.path(), QString(), QStringLiteral(".hidden")));
        QVERIFY(!NewNote::pathFor(root.path(), QStringLiteral("../outside"), QStringLiteral("Plan")));
        QVERIFY(!NewNote::pathFor(root.path(), QStringLiteral("notes/../.."), QStringLiteral("Plan")));
        QVERIFY(!NewNote::pathFor(root.path(), QStringLiteral("/tmp"), QStringLiteral("Plan")));
        QVERIFY(!NewNote::pathFor(root.path(), QStringLiteral(".coeus"), QStringLiteral("Plan")));
        QCOMPARE(NewNote::blank(root.filePath(QStringLiteral("Plan.md"))), QStringLiteral("# Plan\n"));
    }
};

QTEST_GUILESS_MAIN(TestDailyTemplates)
#include "test_daily_templates.moc"
