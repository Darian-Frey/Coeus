#include "ui/Theme.hpp"

#include <QAbstractScrollArea>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QSettings>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleFactory>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace coeus {

namespace {

constexpr const char* kModeKey = "appearance/theme";
constexpr const char* kTextZoomKey = "appearance/textZoom";
constexpr const char* kMonospaceProperty = "coeusMonospace";

} // namespace

Theme& Theme::instance()
{
    static Theme theme;
    return theme;
}

QString Theme::name(Mode mode)
{
    switch (mode) {
    case Mode::Light:
        return QStringLiteral("light");
    case Mode::Dark:
        return QStringLiteral("dark");
    case Mode::Cassette:
        return QStringLiteral("cassette");
    case Mode::DarkCyan:
        return QStringLiteral("dark-cyan");
    case Mode::System:
        break;
    }
    return QStringLiteral("system");
}

Theme::Mode Theme::modeNamed(const QString& name)
{
    if (name == QLatin1String("light"))
        return Mode::Light;
    if (name == QLatin1String("dark"))
        return Mode::Dark;
    if (name == QLatin1String("cassette"))
        return Mode::Cassette;
    if (name == QLatin1String("dark-cyan"))
        return Mode::DarkCyan;
    return Mode::System;
}

void Theme::initialise()
{
    if (m_initialised)
        return;
    m_initialised = true;
    m_systemStyle = QApplication::style()->name();
    m_systemPalette = QApplication::palette();
    m_systemFont = QApplication::font();
    m_mode = modeNamed(QSettings().value(QLatin1String(kModeKey)).toString());
    m_textZoom = QSettings().value(QLatin1String(kTextZoomKey), 1.0).toDouble();
    if (!zoomLevels().contains(m_textZoom))
        m_textZoom = 1.0;
    apply();
    reloadSnippets();
}

const QList<double>& Theme::zoomLevels()
{
    // A browser's steps: finer near 100 %.
    static const QList<double> kLevels{0.5, 0.67, 0.75, 0.8, 0.9, 1.0, 1.1, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0};
    return kLevels;
}

void Theme::setTextZoom(double zoom)
{
    const QList<double>& levels = zoomLevels();
    double nearest = levels.front();
    for (double level : levels)
        if (std::abs(level - zoom) < std::abs(nearest - zoom))
            nearest = level;
    QSettings().setValue(QLatin1String(kTextZoomKey), nearest);
    if (nearest == m_textZoom)
        return;
    m_textZoom = nearest;
    emit textZoomChanged(m_textZoom);
}

void Theme::zoomText(int steps)
{
    if (steps == 0) {
        setTextZoom(1.0);
        return;
    }
    const QList<double>& levels = zoomLevels();
    const qsizetype at = levels.indexOf(m_textZoom);
    setTextZoom(levels[std::clamp<qsizetype>((at < 0 ? levels.indexOf(1.0) : at) + steps, 0, levels.size() - 1)]);
}

QFont Theme::noteFont(bool monospace) const
{
    QFont font = monospace ? QFontDatabase::systemFont(QFontDatabase::FixedFont) : QApplication::font();
    if (font.pointSizeF() > 0)
        font.setPointSizeF(font.pointSizeF() * m_textZoom);
    else if (font.pixelSize() > 0)
        font.setPixelSize(std::max(1, int(std::lround(font.pixelSize() * m_textZoom))));
    return font;
}

bool Theme::zoomFromWheel(QWheelEvent* event)
{
    if (!(event->modifiers() & Qt::ControlModifier))
        return false;
    // A notch is 120; a touchpad sends many small steps, gathered here.
    static int gathered = 0;
    gathered += event->angleDelta().y();
    const int steps = gathered / 120;
    gathered -= steps * 120;
    if (steps != 0)
        instance().zoomText(steps);
    event->accept();
    return true;
}

namespace {

// Ctrl+wheel over a text view's viewport zooms the notes' text.
class WheelZoom : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Wheel && Theme::zoomFromWheel(static_cast<QWheelEvent*>(event)))
            return true;
        return QObject::eventFilter(watched, event);
    }
};

} // namespace

void Theme::followTextZoom(QAbstractScrollArea* view, bool monospace)
{
    const auto apply = [view, monospace] {
        view->setProperty(kMonospaceProperty, monospace);
        view->setFont(instance().noteFont(monospace));
    };
    apply();
    connect(&instance(), &Theme::textZoomChanged, view, apply);
    connect(&instance(), &Theme::changed, view, apply);
    view->viewport()->installEventFilter(new WheelZoom(view));
}

void Theme::setMode(Mode mode)
{
    initialise();
    QSettings().setValue(QLatin1String(kModeKey), name(mode));
    if (mode == m_mode)
        return;
    m_mode = mode;
    apply();
    emit changed();
}

void Theme::apply()
{
    if (m_mode == Mode::System) {
        if (QApplication::style()->name() != m_systemStyle)
            QApplication::setStyle(QStyleFactory::create(m_systemStyle));
        QApplication::setPalette(m_systemPalette);
    } else {
        // Fusion draws with the palette as given; desktop styles may not.
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        QApplication::setPalette(palette(m_mode)); // replaces any per-class palettes too
    }
    if (m_mode == Mode::Cassette) {
        QFont terminal = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        terminal.setPointSizeF(m_systemFont.pointSizeF());
        QApplication::setFont(terminal);
    } else {
        QApplication::setFont(m_systemFont);
    }
    const QString terminal = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
    applySheet();
    releasePalettes();
    if (m_mode != Mode::Cassette && terminal != m_systemFont.family()) {
        // Qt does not always hand back a font a style sheet gave: let any
        // widget still holding Cassette's inherit again, unless it is meant to.
        for (QWidget* widget : QApplication::allWidgets()) {
            if (widget->font().family() == terminal && !widget->property(kMonospaceProperty).toBool())
                widget->setFont(QFont());
        }
    }
}

void Theme::setMonospace(QWidget* widget)
{
    widget->setProperty(kMonospaceProperty, true);
    widget->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
}

void Theme::applySheet()
{
    const QString sheet = styleSheet(m_mode) + m_snippetSheet;
    if (!qApp || qApp->styleSheet() == sheet)
        return;
    qApp->setStyleSheet(sheet);
    releasePalettes();
}

void Theme::releasePalettes()
{
    // Once a style sheet has been applied and removed, some widgets no longer
    // follow the application's palette (Qt 6.4): a later theme left the
    // sidebar in the colours of the one before. Let every widget inherit
    // again; no Coeus widget sets a palette of its own.
    for (QWidget* widget : QApplication::allWidgets())
        widget->setPalette(QPalette());
}

QString Theme::styleSheet(Mode mode)
{
    if (mode == Mode::DarkCyan) {
        // The mockup's flat look: hairline rules, rounded controls, panels a
        // shade lighter than the note, the tool strip and bars a shade more.
        return QStringLiteral(R"(/* Coeus theme: dark cyan */
QMainWindow::separator { background: #2a2e34; width: 1px; height: 1px; }
QDockWidget::title { background: #191c20; color: #6b7280; padding: 6px 10px; border-bottom: 1px solid #2a2e34; text-align: left; }
QTreeView, QTreeWidget, QListView { background: #191c20; border: none; selection-background-color: #262a30; selection-color: #e5e7eb; }
QTreeView::item:hover, QTreeWidget::item:hover, QListView::item:hover { background: #1f2328; }
QTreeView::item:selected, QTreeWidget::item:selected, QListView::item:selected { background: #262a30; color: #e5e7eb; }
QTabBar::tab { background: #1b1e22; color: #8b9199; border: none; border-right: 1px solid #2a2e34; padding: 6px 14px; }
QTabBar::tab:selected { background: #15171a; color: #d6d8dc; }
QTabBar::tab:hover { color: #d6d8dc; }
QTabWidget::pane { border-top: 1px solid #2a2e34; }
QToolBar { background: #1b1e22; border: none; border-right: 1px solid #2a2e34; spacing: 6px; padding: 4px; }
QToolButton { border: 1px solid transparent; border-radius: 5px; padding: 3px; }
QToolButton:hover { background: #262a30; }
QToolButton:checked { background: #262a30; border-color: #2a2e34; color: #f3f4f6; }
QPushButton { background: #191c20; color: #d6d8dc; border: 1px solid #2a2e34; border-radius: 5px; padding: 5px 14px; }
QPushButton:hover { border-color: #3a3f46; background: #1f2328; }
QPushButton:pressed { background: #262a30; }
QPushButton:default { border-color: #3d7d75; }
QPushButton:disabled { color: #4b5058; border-color: #22262b; }
QLineEdit, QComboBox, QKeySequenceEdit { background: #15171a; color: #d6d8dc; border: 1px solid #2a2e34; border-radius: 5px; padding: 3px 6px; selection-background-color: #23524c; selection-color: #f3f4f6; }
QLineEdit:focus, QComboBox:focus { border-color: #5cc2b6; }
QComboBox QAbstractItemView { background: #1b1e22; border: 1px solid #2a2e34; selection-background-color: #262a30; selection-color: #f3f4f6; }
QPlainTextEdit, QTextEdit, QTextBrowser { border: none; selection-background-color: #23524c; selection-color: #f3f4f6; }
QHeaderView::section { background: #191c20; color: #6b7280; border: none; border-bottom: 1px solid #2a2e34; padding: 4px 8px; }
QMenuBar { background: #1b1e22; border-bottom: 1px solid #2a2e34; }
QMenuBar::item { padding: 4px 10px; background: transparent; }
QMenuBar::item:selected, QMenu::item:selected { background: #262a30; color: #f3f4f6; }
QMenu { background: #1b1e22; border: 1px solid #2a2e34; padding: 4px 0; }
QMenu::item { padding: 5px 24px 5px 16px; }
QMenu::separator { height: 1px; background: #2a2e34; margin: 4px 8px; }
QStatusBar { background: #1b1e22; border-top: 1px solid #2a2e34; color: #717780; }
QToolTip { background: #262a30; color: #d6d8dc; border: 1px solid #3a3f46; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }
QScrollBar::handle { background: #2a2e34; border-radius: 4px; min-height: 24px; min-width: 24px; }
QScrollBar::handle:hover { background: #3a3f46; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }
QSplitter::handle { background: #2a2e34; }
)");
    }
    if (mode != Mode::Cassette)
        return {};
    // What a palette cannot say: square corners, ruled edges, terminal headers.
    // The font too: once a style sheet is active, an application font set
    // afterwards does not reach widgets already styled.
    const QString terminal = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
    return QStringLiteral("/* Coeus theme: cassette */\n* { font-family: \"%1\"; }\n").arg(terminal) + QStringLiteral(R"(
QMainWindow::separator { background: #3a2810; width: 2px; height: 2px; }
QDockWidget::title { background: #2a1d0c; color: #ff8c1a; padding: 3px 6px; border-bottom: 1px solid #ff6a00; text-align: left; }
QTabBar::tab { background: #1b140b; color: #b07a2a; border: 1px solid #4a3414; border-bottom: none; padding: 4px 12px; margin-right: 2px; border-radius: 0; }
QTabBar::tab:selected { background: #0f0b06; color: #ffb000; border-color: #ff6a00; border-top: 2px solid #ff6a00; }
QTabBar::tab:hover { color: #ffcc55; }
QTabWidget::pane { border-top: 1px solid #4a3414; }
QToolBar { background: #1b140b; border: none; border-right: 1px solid #4a3414; spacing: 4px; }
QToolButton { border: 1px solid transparent; border-radius: 0; padding: 2px; }
QToolButton:hover { border-color: #ff6a00; background: #2a1d0c; }
QToolButton:checked { border-color: #8a5a1a; background: #2a1d0c; }
QPushButton { background: #2a1d0c; color: #ffb000; border: 1px solid #8a5a1a; border-radius: 0; padding: 4px 12px; }
QPushButton:hover { border-color: #ff6a00; color: #ffcc55; }
QPushButton:pressed { background: #ff6a00; color: #14100a; }
QPushButton:disabled { color: #5a3e18; border-color: #3a2810; }
QLineEdit, QPlainTextEdit, QTextEdit, QTextBrowser, QKeySequenceEdit, QComboBox, QSpinBox { border: 1px solid #4a3414; border-radius: 0; selection-background-color: #ff6a00; selection-color: #14100a; }
QLineEdit:focus, QPlainTextEdit:focus, QComboBox:focus { border-color: #ff6a00; }
QComboBox { background: #1b140b; padding: 2px 6px; }
QComboBox QAbstractItemView { border: 1px solid #ff6a00; background: #0f0b06; selection-background-color: #ff6a00; selection-color: #14100a; }
QTreeView, QTreeWidget, QListView { border: none; selection-background-color: #3a2810; selection-color: #ffcc55; }
QHeaderView::section { background: #1b140b; color: #b07a2a; border: none; border-bottom: 1px solid #4a3414; padding: 3px 6px; }
QMenuBar { background: #14100a; border-bottom: 1px solid #4a3414; }
QMenuBar::item:selected, QMenu::item:selected { background: #ff6a00; color: #14100a; }
QMenu { background: #14100a; border: 1px solid #ff6a00; }
QMenu::separator { height: 1px; background: #4a3414; margin: 3px 6px; }
QStatusBar { background: #1b140b; border-top: 1px solid #ff6a00; color: #b07a2a; }
QToolTip { background: #14100a; color: #ffb000; border: 1px solid #ff6a00; }
QScrollBar:vertical { background: #14100a; width: 10px; margin: 0; }
QScrollBar:horizontal { background: #14100a; height: 10px; margin: 0; }
QScrollBar::handle { background: #4a3414; min-height: 24px; min-width: 24px; }
QScrollBar::handle:hover { background: #8a5a1a; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }
QSplitter::handle { background: #3a2810; }
)");
}

QPalette Theme::palette(Mode mode)
{
    if (mode == Mode::Cassette) {
        const QColor amber(0xff, 0xb0, 0x00);
        const QColor dim(0x8a, 0x5a, 0x1a);
        QPalette p;
        p.setColor(QPalette::Window, QColor(0x14, 0x10, 0x0a));
        p.setColor(QPalette::WindowText, amber);
        p.setColor(QPalette::Base, QColor(0x0f, 0x0b, 0x06));
        p.setColor(QPalette::AlternateBase, QColor(0x1b, 0x14, 0x0b));
        p.setColor(QPalette::Text, amber);
        p.setColor(QPalette::PlaceholderText, dim);
        p.setColor(QPalette::Button, QColor(0x2a, 0x1d, 0x0c));
        p.setColor(QPalette::ButtonText, amber);
        p.setColor(QPalette::BrightText, QColor(0xff, 0xe0, 0x8a));
        p.setColor(QPalette::Highlight, QColor(0xff, 0x6a, 0x00));
        p.setColor(QPalette::HighlightedText, QColor(0x14, 0x10, 0x0a));
        p.setColor(QPalette::Link, QColor(0xff, 0x8c, 0x1a));
        p.setColor(QPalette::LinkVisited, QColor(0xff, 0x8c, 0x1a));
        p.setColor(QPalette::ToolTipBase, QColor(0x14, 0x10, 0x0a));
        p.setColor(QPalette::ToolTipText, amber);
        p.setColor(QPalette::Light, QColor(0x4a, 0x34, 0x14));
        p.setColor(QPalette::Midlight, QColor(0x3a, 0x28, 0x10));
        p.setColor(QPalette::Mid, QColor(0x2a, 0x1d, 0x0c));
        p.setColor(QPalette::Dark, QColor(0x0a, 0x07, 0x04));
        p.setColor(QPalette::Shadow, QColor(0, 0, 0));
        for (const QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
            p.setColor(QPalette::Disabled, role, QColor(0x5a, 0x3e, 0x18));
        return p;
    }
    if (mode == Mode::DarkCyan) {
        // Measured from docs/design/main-window.png.
        const QColor text(0xd6, 0xd8, 0xdc);
        const QColor dim(0x6b, 0x72, 0x80);
        QPalette p;
        p.setColor(QPalette::Window, QColor(0x1b, 0x1e, 0x22));        // title, tool strip, status bar
        p.setColor(QPalette::WindowText, text);
        p.setColor(QPalette::Base, QColor(0x15, 0x17, 0x1a));          // the note
        p.setColor(QPalette::AlternateBase, QColor(0x19, 0x1c, 0x20)); // panels
        p.setColor(QPalette::Text, text);
        p.setColor(QPalette::PlaceholderText, dim);
        p.setColor(QPalette::Button, QColor(0x19, 0x1c, 0x20));
        p.setColor(QPalette::ButtonText, text);
        p.setColor(QPalette::BrightText, QColor(0xf3, 0xf4, 0xf6));
        p.setColor(QPalette::Highlight, QColor(0x23, 0x52, 0x4c));
        p.setColor(QPalette::HighlightedText, QColor(0xf3, 0xf4, 0xf6));
        p.setColor(QPalette::Link, QColor(0x5c, 0xc2, 0xb6));
        p.setColor(QPalette::LinkVisited, QColor(0x5c, 0xc2, 0xb6));
        p.setColor(QPalette::ToolTipBase, QColor(0x26, 0x2a, 0x30));
        p.setColor(QPalette::ToolTipText, text);
        p.setColor(QPalette::Light, QColor(0x3a, 0x3f, 0x46));
        p.setColor(QPalette::Midlight, QColor(0x2a, 0x2e, 0x34));
        p.setColor(QPalette::Mid, QColor(0x2a, 0x2e, 0x34));
        p.setColor(QPalette::Dark, QColor(0x10, 0x12, 0x14));
        p.setColor(QPalette::Shadow, QColor(0, 0, 0));
        for (const QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
            p.setColor(QPalette::Disabled, role, QColor(0x4b, 0x50, 0x58));
        p.setColor(QPalette::Disabled, QPalette::Highlight, dim);
        return p;
    }
    const bool dark = mode == Mode::Dark;
    const QColor window = dark ? QColor(0x23, 0x26, 0x29) : QColor(0xf4, 0xf5, 0xf6);
    const QColor base = dark ? QColor(0x1b, 0x1e, 0x20) : QColor(0xff, 0xff, 0xff);
    const QColor alternate = dark ? QColor(0x26, 0x2a, 0x2d) : QColor(0xf0, 0xf2, 0xf3);
    const QColor text = dark ? QColor(0xe2, 0xe4, 0xe6) : QColor(0x1d, 0x1f, 0x21);
    const QColor dim = dark ? QColor(0x8a, 0x92, 0x99) : QColor(0x6b, 0x72, 0x78);
    const QColor button = dark ? QColor(0x2c, 0x30, 0x34) : QColor(0xec, 0xee, 0xf0);
    const QColor accent = dark ? QColor(0x3f, 0x9e, 0x93) : QColor(0x1f, 0x7a, 0x70);
    const QColor link = dark ? QColor(0x5f, 0xc4, 0xb8) : QColor(0x1f, 0x7a, 0x70);

    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, alternate);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::PlaceholderText, dim);
    p.setColor(QPalette::Button, button);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, dark ? QColor(0xff, 0x8a, 0x80) : QColor(0xc6, 0x28, 0x28));
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
    p.setColor(QPalette::Link, link);
    p.setColor(QPalette::LinkVisited, link);
    p.setColor(QPalette::ToolTipBase, dark ? QColor(0x32, 0x37, 0x3b) : QColor(0xff, 0xff, 0xf2));
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Light, dark ? QColor(0x3a, 0x3f, 0x44) : QColor(0xff, 0xff, 0xff));
    p.setColor(QPalette::Midlight, dark ? QColor(0x33, 0x38, 0x3c) : QColor(0xe3, 0xe5, 0xe8));
    p.setColor(QPalette::Mid, dark ? QColor(0x2a, 0x2e, 0x32) : QColor(0xc4, 0xc8, 0xcc));
    p.setColor(QPalette::Dark, dark ? QColor(0x14, 0x16, 0x18) : QColor(0xa0, 0xa5, 0xaa));
    p.setColor(QPalette::Shadow, QColor(0, 0, 0));
    for (const QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, dim);
    p.setColor(QPalette::Disabled, QPalette::Highlight, dim);
    return p;
}

bool Theme::isDark() const
{
    return QApplication::palette().color(QPalette::Base).lightness() < 128;
}

QColor Theme::category(int index) const
{
    index = std::max(index, 0);
    const QColor accent = colour(Colour::Accent);
    if (m_mode == Mode::Cassette) {
        // Phosphor shades: ambers to reds to yellows, each at three brightnesses.
        const int hue = 8 + (index * 11) % 44;
        const int lightness = 150 - (index / 4 % 3) * 30;
        return QColor::fromHsl(hue, 230, lightness);
    }
    const int hue = (std::max(accent.hslHue(), 0) + index * 137) % 360;
    return isDark() ? QColor::fromHsl(hue, 150, 150) : QColor::fromHsl(hue, 170, 105);
}

QColor Theme::calloutFill(const QColor& kind) const
{
    if (m_mode != Mode::DarkCyan || !kind.isValid())
        return {};
    // The kind's colour, a quarter strength, over the note: the mockup's
    // decision box is teal on near-black.
    const QColor base = QApplication::palette().color(QPalette::Base);
    const auto mix = [](int over, int under) { return int(std::lround(under + 0.22 * (over - under))); };
    return QColor(mix(kind.red(), base.red()), mix(kind.green(), base.green()), mix(kind.blue(), base.blue()));
}

QColor Theme::colour(Colour colour) const
{
    if (m_mode == Mode::DarkCyan) {
        switch (colour) {
        case Colour::Accent:
            return QColor(0x5c, 0xc2, 0xb6); // the mockup's teal
        case Colour::Error:
            return QColor(0xef, 0x6b, 0x61);
        case Colour::Warning:
            return QColor(0xe0, 0xa4, 0x4c); // the mockup's amber
        case Colour::Info:
            return QColor(0x64, 0xa8, 0xf0);
        case Colour::Added:
            return QColor(0x3f, 0xb9, 0x50, 0x40);
        case Colour::Removed:
            return QColor(0xf8, 0x51, 0x49, 0x40);
        case Colour::Highlight:
            return QColor(0x5a, 0x48, 0x1c);
        case Colour::Tint:
            return QColor(0x22, 0x26, 0x2b); // behind code, as the mockup's .md
        case Colour::CodeKeyword:
            return QColor(0xc6, 0x92, 0xf0);
        case Colour::CodeType:
            return QColor(0x5c, 0xc2, 0xb6);
        case Colour::CodeString:
            return QColor(0xe0, 0xa8, 0x6a);
        case Colour::CodeNumber:
            return QColor(0x7f, 0xb8, 0xf0);
        }
    }
    if (m_mode == Mode::Cassette) {
        switch (colour) {
        case Colour::Accent:
            return QColor(0xff, 0x8c, 0x1a); // burnt orange
        case Colour::Error:
            return QColor(0xff, 0x3b, 0x1f);
        case Colour::Warning:
            return QColor(0xff, 0xe0, 0x8a); // pale phosphor
        case Colour::Info:
            return QColor(0xc8, 0x9a, 0x4a);
        case Colour::Added:
            return QColor(0x9d, 0xc4, 0x3a, 0x40);
        case Colour::Removed:
            return QColor(0xff, 0x3b, 0x1f, 0x40);
        case Colour::Highlight:
            return QColor(0x5a, 0x3a, 0x00);
        case Colour::Tint:
            return QColor(0x24, 0x1a, 0x0d);
        case Colour::CodeKeyword:
            return QColor(0xff, 0x8c, 0x1a);
        case Colour::CodeType:
            return QColor(0xff, 0xd2, 0x6a);
        case Colour::CodeString:
            return QColor(0x9d, 0xc4, 0x3a); // phosphor green, the one cool note
        case Colour::CodeNumber:
            return QColor(0xff, 0xe0, 0x8a);
        }
    }
    const bool dark = isDark();
    switch (colour) {
    case Colour::Accent:
        return dark ? QColor(0x5f, 0xc4, 0xb8) : QColor(0x1f, 0x7a, 0x70);
    case Colour::Error:
        return dark ? QColor(0xef, 0x6b, 0x61) : QColor(0xc6, 0x28, 0x28);
    case Colour::Warning:
        return dark ? QColor(0xf0, 0xa8, 0x3a) : QColor(0xb8, 0x6e, 0x00);
    case Colour::Info:
        return dark ? QColor(0x64, 0xa8, 0xf0) : QColor(0x19, 0x76, 0xd2);
    case Colour::Added:
        return dark ? QColor(0x3f, 0xb9, 0x50, 0x40) : QColor(0x2e, 0xa0, 0x43, 0x38);
    case Colour::Removed:
        return dark ? QColor(0xf8, 0x51, 0x49, 0x40) : QColor(0xd3, 0x2f, 0x2f, 0x38);
    case Colour::Highlight:
        return dark ? QColor(0x7a, 0x6a, 0x1c) : QColor(0xff, 0xec, 0x8a);
    case Colour::Tint:
        return dark ? QColor(0x2a, 0x2f, 0x33) : QColor(0xf1, 0xf3, 0xf4);
    case Colour::CodeKeyword:
        return dark ? QColor(0xc6, 0x92, 0xf0) : QColor(0x8a, 0x3f, 0xb8);
    case Colour::CodeType:
        return dark ? QColor(0x5f, 0xc4, 0xb8) : QColor(0x1f, 0x7a, 0x70);
    case Colour::CodeString:
        return dark ? QColor(0xe0, 0xa8, 0x6a) : QColor(0xa3, 0x51, 0x1a);
    case Colour::CodeNumber:
        return dark ? QColor(0x7f, 0xb8, 0xf0) : QColor(0x1f, 0x5f, 0xb0);
    }
    return {};
}

QString Theme::snippetsFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/snippets");
}

QStringList Theme::reloadSnippets()
{
    QStringList report;
    QString sheet;
    m_snippets.clear();
    const QDir folder(snippetsFolder());
    for (const QString& file : folder.entryList({QStringLiteral("*.qss")}, QDir::Files, QDir::Name)) {
        QFile in(folder.filePath(file));
        if (!in.open(QIODevice::ReadOnly)) {
            report.append(tr("%1 could not be read: %2").arg(file, in.errorString()));
            continue;
        }
        sheet += QStringLiteral("/* %1 */\n").arg(file) + QString::fromUtf8(in.readAll()) + QLatin1Char('\n');
        m_snippets.append(file);
    }
    if (sheet != m_snippetSheet) {
        m_snippetSheet = sheet;
        applySheet();
        emit changed();
    }
    report.prepend(m_snippets.isEmpty() ? tr("No style snippets in %1").arg(snippetsFolder())
                                        : tr("Style snippets applied: %1").arg(m_snippets.join(QStringLiteral(", "))));
    return report;
}

} // namespace coeus
