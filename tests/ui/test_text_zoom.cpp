// The notes' text zoom: View › Zoom In, Zoom Out and Reset Zoom, and
// Ctrl+wheel, sizing Source, Live and Reading alike, kept per user.

#include "ui/Editor.hpp"
#include "ui/MainWindow.hpp"
#include "ui/NoteView.hpp"
#include "ui/Theme.hpp"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QSettings>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBrowser>
#include <QWheelEvent>

using namespace coeus;

namespace {

QAction* actionNamed(MainWindow& window, const QString& text)
{
    for (QAction* action : window.registeredActions())
        if (action->text().remove(QLatin1Char('&')) == text)
            return action;
    return nullptr;
}

double points(const QFont& font)
{
    return font.pointSizeF();
}

// One notch of the wheel, with Ctrl held or not.
void wheel(QWidget* target, int notches, bool ctrl)
{
    const QPointF at(target->width() / 2.0, target->height() / 2.0);
    QWheelEvent event(at, target->mapToGlobal(at), QPoint(), QPoint(0, 120 * notches), Qt::NoButton,
                      ctrl ? Qt::ControlModifier : Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(target, &event);
}

} // namespace

class TestTextZoom : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_settings;
    QTemporaryDir m_vault;

private slots:
    void initTestCase()
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        QFile note(m_vault.filePath(QStringLiteral("a.md")));
        QVERIFY(note.open(QIODevice::WriteOnly));
        note.write("# A heading\n\nSome text to read.\n");
    }

    void levelsStepAndAreKept()
    {
        Theme& theme = Theme::instance();
        theme.zoomText(0);
        QCOMPARE(theme.textZoom(), 1.0);
        QSignalSpy changed(&theme, &Theme::textZoomChanged);
        theme.zoomText(1);
        QCOMPARE(theme.textZoom(), 1.1);
        theme.zoomText(-2);
        QCOMPARE(theme.textZoom(), 0.9);
        QCOMPARE(changed.size(), 2);
        theme.setTextZoom(1.23); // the nearest level
        QCOMPARE(theme.textZoom(), 1.25);
        QCOMPARE(QSettings().value(QStringLiteral("appearance/textZoom")).toDouble(), 1.25);
        for (int i = 0; i < 20; ++i)
            theme.zoomText(1);
        QCOMPARE(theme.textZoom(), Theme::zoomLevels().back()); // no further
        for (int i = 0; i < 20; ++i)
            theme.zoomText(-1);
        QCOMPARE(theme.textZoom(), Theme::zoomLevels().front());
        theme.zoomText(0);
        QCOMPARE(theme.textZoom(), 1.0);
        // The note font is the interface's, or fixed pitch, scaled.
        theme.setTextZoom(1.5);
        QCOMPARE(points(theme.noteFont(false)), points(QApplication::font()) * 1.5);
        QCOMPARE(points(theme.noteFont(true)), points(QFontDatabase::systemFont(QFontDatabase::FixedFont)) * 1.5);
        theme.zoomText(0);
    }

    void everyModeFollowsTheZoom()
    {
        Theme::instance().zoomText(0);
        MainWindow window;
        window.resize(1000, 700);
        window.show();
        window.openVault(*Vault::open(m_vault.path()));
        window.openNote(m_vault.filePath(QStringLiteral("a.md")));
        NoteView* view = window.currentView();
        QVERIFY(view);
        Editor* editor = view->editor();
        const double fixed = points(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        const double prose = points(QApplication::font());

        QAction* zoomIn = actionNamed(window, QStringLiteral("Zoom In"));
        QAction* zoomOut = actionNamed(window, QStringLiteral("Zoom Out"));
        QAction* reset = actionNamed(window, QStringLiteral("Reset Zoom"));
        QVERIFY(zoomIn && zoomOut && reset);
        QCOMPARE(zoomIn->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_Equal));
        QCOMPARE(zoomOut->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_Minus));
        QCOMPARE(reset->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_0));

        // Source: fixed pitch, scaled.
        view->setMode(NoteView::Mode::Source);
        zoomIn->trigger();
        zoomIn->trigger();
        QCOMPARE(Theme::instance().textZoom(), 1.25);
        QCOMPARE(points(editor->font()), fixed * 1.25);
        QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("125 %")) || !window.isActiveWindow());
        // Live: the interface's font, scaled, and what it draws with it.
        view->setMode(NoteView::Mode::Live);
        QCOMPARE(points(editor->font()), prose * 1.25);
        QCOMPARE(points(editor->document()->defaultFont()), prose * 1.25);
        // Reading: the same.
        view->setMode(NoteView::Mode::Reading);
        QCOMPARE(points(view->reader()->font()), prose * 1.25);

        // Ctrl+wheel over the reader and the editor zooms; a plain wheel scrolls.
        wheel(view->reader()->viewport(), 1, true);
        QCOMPARE(Theme::instance().textZoom(), 1.5);
        QCOMPARE(points(view->reader()->font()), prose * 1.5);
        view->setMode(NoteView::Mode::Source);
        wheel(editor->viewport(), -1, true);
        QCOMPARE(Theme::instance().textZoom(), 1.25);
        wheel(editor->viewport(), -1, false);
        QCOMPARE(Theme::instance().textZoom(), 1.25);

        // A theme change, Cassette and back, keeps the zoomed fonts.
        view->setMode(NoteView::Mode::Live);
        Theme::instance().setMode(Theme::Mode::Cassette);
        Theme::instance().setMode(Theme::Mode::System);
        QCOMPARE(points(editor->font()), prose * 1.25);
        QCOMPARE(points(view->reader()->font()), prose * 1.25);

        // Kept for the next window, until reset.
        QCOMPARE(QSettings().value(QStringLiteral("appearance/textZoom")).toDouble(), 1.25);
        reset->trigger();
        QCOMPARE(points(editor->font()), prose);
        zoomOut->trigger();
        QCOMPARE(Theme::instance().textZoom(), 0.9);
        reset->trigger();
    }
};

QTEST_MAIN(TestTextZoom)
#include "test_text_zoom.moc"
