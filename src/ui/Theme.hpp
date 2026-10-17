#pragma once

#include <QColor>
#include <QFont>
#include <QObject>
#include <QPalette>
#include <QString>
#include <QStringList>

class QWidget;

class QAbstractScrollArea;
class QWheelEvent;

namespace coeus {

// Light and dark themes and user style snippets (F-027, SPEC.md §Themes).
// One per application: the mode sets the application's style and palette,
// and the semantic colours every view draws with follow whichever palette is
// in use, so a view built later, or redrawn on changed(), matches it. Both
// are per-user preferences in Coeus's settings, never in a vault.
class Theme : public QObject {
    Q_OBJECT

public:
    // Cassette: cassette futurism, an amber CRT terminal (warm near-black,
    // amber phosphor, burnt orange, monospace, square ruled edges).
    enum class Mode { System, Light, Dark, Cassette };
    enum class Colour {
        Accent,    // links, tags, list markers: the mockup's teal
        Error,     // lint errors, refusals
        Warning,   // lint warnings, a stale review
        Info,      // lint info
        Added,     // diff: a translucent green
        Removed,   // diff: a translucent red
        Highlight, // ==marked== text
        Tint,      // behind code and callouts
        CodeKeyword, CodeType, CodeString, CodeNumber, // fenced code by language (F-025); comments are dim
    };

    static Theme& instance();

    // Applies the saved mode and snippets; once, after the QApplication exists.
    void initialise();
    Mode mode() const { return m_mode; }
    // Applies and saves `mode`; emits changed() if anything changed.
    void setMode(Mode mode);
    // Whether the palette in use is dark, whatever the mode.
    bool isDark() const;
    QColor colour(Colour colour) const;
    // The `index`th of a run of distinct colours for groups, such as the graph's
    // projects and tags (F-026): hues turned from the accent by the golden
    // angle, as light as the palette needs; Cassette's stay among the ambers.
    QColor category(int index) const;
    static QPalette palette(Mode mode); // Light, Dark or Cassette; System is the desktop's own
    // The style sheet a mode brings itself, before the user's snippets; empty for most.
    static QString styleSheet(Mode mode);
    // Gives `widget` the fixed-pitch font for good: an editor, a diff. Other
    // widgets that still hold Cassette's font after it is left are reset.
    static void setMonospace(QWidget* widget);
    // A label's style sheet for an error message.
    QString errorStyle() const { return QStringLiteral("color: %1;").arg(colour(Colour::Error).name()); }

    // Text size in the note views (Source, Live, Reading, versions and
    // diffs), as a share of the interface's: View › Zoom In, Zoom Out and
    // Reset Zoom, or Ctrl+wheel over a note. One for every window, kept per
    // user (`appearance/textZoom`); emits textZoomChanged().
    double textZoom() const { return m_textZoom; }
    void setTextZoom(double zoom); // the nearest level
    void zoomText(int steps);      // +1 the next level up, -1 down, 0 back to 100 %
    static const QList<double>& zoomLevels();
    // The font a note view draws its text in: the interface's, or the
    // fixed-pitch one, at the text zoom.
    QFont noteFont(bool monospace) const;
    // Ctrl+wheel over a note view zooms its text; true if `event` did.
    static bool zoomFromWheel(QWheelEvent* event);
    // Gives `view` (a reading view, a version, a diff) the note font at the
    // text zoom, now and after every zoom or theme change, and lets Ctrl+wheel
    // over it zoom.
    static void followTextZoom(QAbstractScrollArea* view, bool monospace);

    // Style snippets: every *.qss file in snippetsFolder(), in name order,
    // applied as the application's style sheet (Obsidian's CSS snippets).
    static QString snippetsFolder();
    QStringList snippets() const { return m_snippets; } // file names applied
    // Reads the folder again; what it found, or why a file was left out.
    QStringList reloadSnippets();

    static QString name(Mode mode);
    static Mode modeNamed(const QString& name);

signals:
    void changed();
    void textZoomChanged(double zoom);

private:
    Theme() = default;
    void apply();
    void applySheet(); // the mode's own sheet, then the snippets
    static void releasePalettes(); // every widget follows the application's palette again

    QString m_snippetSheet;
    QFont m_systemFont;
    double m_textZoom = 1.0;
    Mode m_mode = Mode::System;
    bool m_initialised = false;
    QString m_systemStyle;    // the desktop's style and palette, restored by System
    QPalette m_systemPalette;
    QStringList m_snippets;
};

} // namespace coeus
