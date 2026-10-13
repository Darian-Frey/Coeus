// F-009 and F-010 in the window: Ctrl+D opens or creates today's note;
// Insert Template puts a template in at the cursor as one undo step.

#include "ui/CommandPalette.hpp"
#include "ui/Editor.hpp"
#include "ui/MainWindow.hpp"

#include <QAction>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using namespace coeus;

namespace {

void write(const QString& path, const QByteArray& content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write(content);
}

QAction* actionNamed(MainWindow& window, const QString& text)
{
    for (QAction* action : window.registeredActions()) {
        if (action->text().remove(QLatin1Char('&')) == text)
            return action;
    }
    return nullptr;
}

} // namespace

class TestDailyTemplateCommands : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_settings;

private slots:
    void initTestCase()
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    }

    void ctrlDOpensTodaysNote()
    {
        QTemporaryDir vault;
        MainWindow window;
        window.openVault(*Vault::open(vault.path()));
        QAction* today = actionNamed(window, QStringLiteral("Open Today's Note"));
        QVERIFY(today);
        QCOMPARE(today->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_D));

        today->trigger();
        const QString expected = vault.filePath(QStringLiteral("daily/%1.md").arg(QDate::currentDate().toString(Qt::ISODate)));
        QVERIFY(QFile::exists(expected));
        QCOMPARE(window.currentEditor()->path(), expected);

        // Again: the same note, not a second one, and not rewritten.
        window.currentEditor()->insertPlainText(QStringLiteral("typed "));
        QVERIFY(window.saveCurrent());
        today->trigger();
        QCOMPARE(window.tabs()->count(), 1);
        QFile file(expected);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().startsWith("typed "));
    }

    void insertTemplateAtTheCursor()
    {
        QTemporaryDir vault;
        write(vault.filePath(QStringLiteral("templates/Meeting.md")), "---\ntags: [meeting]\n---\n## {{title}} agenda\n");
        write(vault.filePath(QStringLiteral("templates/Other.md")), "other\n");
        write(vault.filePath(QStringLiteral("Standup.md")), "---\nstatus: Active\n---\n# Standup\n\n");
        MainWindow window;
        window.openVault(*Vault::open(vault.path()));
        window.openNote(vault.filePath(QStringLiteral("Standup.md")));
        Editor* editor = window.currentEditor();
        editor->moveCursor(QTextCursor::End);
        const QString before = editor->toPlainText();

        actionNamed(window, QStringLiteral("Insert Template…"))->trigger();
        CommandPalette* picker = window.templatePicker();
        QCOMPARE(picker->visibleCount(), 2);
        picker->setFilterText(QStringLiteral("meet"));
        QCOMPARE(picker->visibleCount(), 1);
        QTest::keyClick(picker->focusWidget(), Qt::Key_Return);

        QCOMPARE(editor->toPlainText(),
                 QStringLiteral("---\nstatus: Active\ntags: [meeting]\n---\n# Standup\n\n## Standup agenda\n"));
        QVERIFY(editor->isModified()); // in the buffer; saved like any edit
        editor->undo();
        QCOMPARE(editor->toPlainText(), before); // one step
    }

    void noTemplatesSaysSo()
    {
        QTemporaryDir vault;
        write(vault.filePath(QStringLiteral("a.md")), "x\n");
        MainWindow window;
        window.openVault(*Vault::open(vault.path()));
        window.openNote(vault.filePath(QStringLiteral("a.md")));
        actionNamed(window, QStringLiteral("Insert Template…"))->trigger();
        QVERIFY(!window.templatePicker()->isVisible());
        QCOMPARE(window.currentEditor()->toPlainText(), QStringLiteral("x\n"));
    }
};

QTEST_MAIN(TestDailyTemplateCommands)
#include "test_daily_template_commands.moc"
