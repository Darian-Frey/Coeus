// F-027: light and dark themes that every view follows, style snippets, and
// rebindable shortcuts that never collide and survive a restart.

#include "ui/DiffView.hpp"
#include "ui/MainWindow.hpp"
#include "ui/NoteCatalogue.hpp"
#include "ui/MarkdownHighlighter.hpp"
#include "ui/Shortcuts.hpp"
#include "ui/ShortcutsDialog.hpp"
#include "ui/Theme.hpp"

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QFontInfo>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSignalSpy>
#include <QStyle>
#include <QTemporaryDir>
#include <QTest>
#include <QTextDocument>
#include <QTreeWidget>

using namespace coeus;

namespace {

QAction* actionNamed(MainWindow& window, const QString& text)
{
    for (QAction* action : window.registeredActions())
        if (action->text().remove(QLatin1Char('&')) == text)
            return action;
    return nullptr;
}

void write(const QString& path, const QByteArray& content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write(content);
}

bool selectRow(ShortcutsDialog& dialog, const QString& command)
{
    for (int i = 0; i < dialog.tree()->topLevelItemCount(); ++i) {
        if (dialog.tree()->topLevelItem(i)->text(0) == command) {
            dialog.tree()->setCurrentItem(dialog.tree()->topLevelItem(i));
            return true;
        }
    }
    return false;
}

} // namespace

class TestThemeShortcuts : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_settings;
    QTemporaryDir m_config;

private slots:
    void initTestCase()
    {
        qputenv("XDG_CONFIG_HOME", QFile::encodeName(m_config.path())); // the snippets folder
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    }

    void darkAndLightReachEveryView()
    {
        MainWindow window;
        Theme& theme = Theme::instance();
        const QColor systemBase = QApplication::palette().color(QPalette::Base);
        const QString systemStyle = QApplication::style()->name();
        QCOMPARE(theme.mode(), Theme::Mode::System);
        QAction* dark = actionNamed(window, QStringLiteral("Theme: Dark"));
        QVERIFY(dark);

        QTextDocument document(QStringLiteral("A [[link]] and #tag."));
        MarkdownHighlighter highlighter(&document);
        DiffView diff(QStringLiteral("/n.md"), QStringLiteral("old"), QStringLiteral("new"),
                      {{DiffLine::Kind::Added, 0, 1, QStringLiteral("added")}});
        dark->trigger();
        QCOMPARE(theme.mode(), Theme::Mode::Dark);
        QVERIFY(theme.isDark());
        QCOMPARE(QApplication::style()->name().toLower(), QStringLiteral("fusion"));
        QCOMPARE(QApplication::palette().color(QPalette::Base), Theme::palette(Theme::Mode::Dark).color(QPalette::Base));
        QCOMPARE(QSettings().value(QStringLiteral("appearance/theme")).toString(), QStringLiteral("dark"));
        QVERIFY(dark->isChecked());
        // Views built before the change follow it.
        QCOMPARE(highlighter.format(MarkdownHighlighter::Kind::Link).foreground().color(), theme.colour(Theme::Colour::Accent));
        QCOMPARE(diff.findChild<QPlainTextEdit*>()->extraSelections().front().format.background().color(),
                 theme.colour(Theme::Colour::Added));
        const QColor darkAccent = theme.colour(Theme::Colour::Accent);

        actionNamed(window, QStringLiteral("Theme: Light"))->trigger();
        QVERIFY(!theme.isDark());
        QVERIFY(theme.colour(Theme::Colour::Accent) != darkAccent);
        QCOMPARE(highlighter.format(MarkdownHighlighter::Kind::Link).foreground().color(), theme.colour(Theme::Colour::Accent));

        // Cassette: amber on warm black, monospace, its own style sheet.
        const QFont systemFont = QApplication::font();
        actionNamed(window, QStringLiteral("Theme: Cassette"))->trigger();
        QCOMPARE(theme.mode(), Theme::Mode::Cassette);
        QVERIFY(theme.isDark());
        QCOMPARE(QApplication::palette().color(QPalette::Text), QColor(0xff, 0xb0, 0x00));
        QVERIFY(QFontInfo(QApplication::font()).fixedPitch());
        QVERIFY(qApp->styleSheet().startsWith(QLatin1String("/* Coeus theme: cassette */")));
        QCOMPARE(highlighter.format(MarkdownHighlighter::Kind::Link).foreground().color(), QColor(0xff, 0x8c, 0x1a));
        QCOMPARE(QSettings().value(QStringLiteral("appearance/theme")).toString(), QStringLiteral("cassette"));

        // System gives the desktop its look back: style, palette, font, no sheet.
        actionNamed(window, QStringLiteral("Theme: System"))->trigger();
        QCOMPARE(QApplication::palette().color(QPalette::Base), systemBase);
        QCOMPARE(QApplication::style()->name(), systemStyle);
        QCOMPARE(QApplication::font(), systemFont);
        QVERIFY(qApp->styleSheet().isEmpty());
        // No widget keeps Cassette's font, except those meant to be fixed-pitch.
        const QString terminal = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
        if (terminal != systemFont.family()) {
            for (QWidget* widget : window.findChildren<QWidget*>()) {
                if (widget->font().family() == terminal)
                    QVERIFY2(widget->property("coeusMonospace").toBool() || qobject_cast<QPlainTextEdit*>(widget->parentWidget()),
                             widget->metaObject()->className());
            }
        }
        QCOMPARE(QSettings().value(QStringLiteral("appearance/theme")).toString(), QStringLiteral("system"));
    }

    void snippetsStyleTheApplication()
    {
        const QString folder = Theme::snippetsFolder();
        QVERIFY(folder.startsWith(m_config.path()));
        write(folder + QStringLiteral("/b-tabs.qss"), "QTabBar::tab { padding: 6px; }\n");
        write(folder + QStringLiteral("/a-tree.qss"), "QTreeView { font-size: 13px; }\n");
        write(folder + QStringLiteral("/notes.txt"), "not a snippet\n");
        const QStringList report = Theme::instance().reloadSnippets();
        QCOMPARE(Theme::instance().snippets(), (QStringList{QStringLiteral("a-tree.qss"), QStringLiteral("b-tabs.qss")}));
        QVERIFY(report.front().contains(QLatin1String("a-tree.qss, b-tabs.qss")));
        const QString sheet = qApp->styleSheet();
        QVERIFY(sheet.indexOf(QLatin1String("QTreeView")) < sheet.indexOf(QLatin1String("QTabBar"))); // in name order
        // A theme's own sheet comes first, so snippets can override it.
        Theme::instance().setMode(Theme::Mode::Cassette);
        QVERIFY(qApp->styleSheet().startsWith(QLatin1String("/* Coeus theme: cassette */")));
        QVERIFY(qApp->styleSheet().indexOf(QLatin1String("a-tree.qss")) > 0);
        Theme::instance().setMode(Theme::Mode::System);
        QFile::remove(folder + QStringLiteral("/a-tree.qss"));
        QFile::remove(folder + QStringLiteral("/b-tabs.qss"));
        Theme::instance().reloadSnippets();
        QVERIFY(qApp->styleSheet().isEmpty());
    }

    void shortcutsAreRebindableNeverShared()
    {
        auto window = std::make_unique<MainWindow>();
        QAction* newNote = actionNamed(*window, QStringLiteral("New Note…"));
        QVERIFY(newNote);
        QCOMPARE(newNote->shortcut(), QKeySequence(QKeySequence::New));
        window->editShortcuts();
        auto* dialog = window->findChild<ShortcutsDialog*>(QStringLiteral("shortcutsDialog"));
        QVERIFY(dialog);
        QCOMPARE(dialog->tree()->topLevelItemCount(), int(window->registeredActions().size())); // every registered command
        dialog->filterEdit()->setText(QStringLiteral("new note"));
        QVERIFY(selectRow(*dialog, QStringLiteral("New Note…")));

        // Another command's keys are refused, and nothing changes.
        QVERIFY(!dialog->assign(QKeySequence::Save));
        QVERIFY(dialog->message().contains(QLatin1String("Save")));
        QCOMPARE(newNote->shortcut(), QKeySequence(QKeySequence::New));

        const QKeySequence chosen(Qt::CTRL | Qt::ALT | Qt::Key_N);
        QVERIFY(dialog->assign(chosen));
        QCOMPARE(newNote->shortcut(), chosen);
        QCOMPARE(dialog->keyEdit()->keySequence(), chosen);
        QCOMPARE(QSettings().value(QStringLiteral("shortcuts/New Note")).toString(), chosen.toString(QKeySequence::PortableText));
        dialog->close();

        // A new window (a restart) keeps the choice; the default is remembered.
        window = std::make_unique<MainWindow>();
        newNote = actionNamed(*window, QStringLiteral("New Note…"));
        QCOMPARE(newNote->shortcut(), chosen);
        QCOMPARE(Shortcuts::defaultOf(newNote), QKeySequence(QKeySequence::New));

        // Ctrl+N is free now, so another command may take it.
        QAction* today = actionNamed(*window, QStringLiteral("Open Today's Note"));
        QVERIFY(!Shortcuts::conflict(window->registeredActions(), today, QKeySequence::New));
        // Clear, then Reset restores the default and forgets the choice.
        Shortcuts::set(newNote, QKeySequence());
        QVERIFY(newNote->shortcut().isEmpty());
        Shortcuts::reset(newNote);
        QCOMPARE(newNote->shortcut(), QKeySequence(QKeySequence::New));
        QVERIFY(!QSettings().contains(QStringLiteral("shortcuts/New Note")));
    }

    void noTwoCommandsShareADefault()
    {
        MainWindow window;
        const QList<QAction*> actions = window.registeredActions();
        for (QAction* action : actions) {
            QAction* other = Shortcuts::conflict(actions, action, action->shortcut());
            QVERIFY2(!other, qPrintable(QStringLiteral("%1 and %2 share %3").arg(action->text(), other ? other->text() : QString(),
                                                                                 action->shortcut().toString())));
        }
    }
};

QTEST_MAIN(TestThemeShortcuts)
#include "test_theme_shortcuts.moc"
