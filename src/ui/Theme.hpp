#pragma once

#include <QColor>
#include <QFont>
#include <QObject>
#include <QPalette>
#include <QString>
#include <QStringList>

class QWidget;

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
    static QPalette palette(Mode mode); // Light, Dark or Cassette; System is the desktop's own
    // The style sheet a mode brings itself, before the user's snippets; empty for most.
    static QString styleSheet(Mode mode);
    // Gives `widget` the fixed-pitch font for good: an editor, a diff. Other
    // widgets that still hold Cassette's font after it is left are reset.
    static void setMonospace(QWidget* widget);
    // A label's style sheet for an error message.
    QString errorStyle() const { return QStringLiteral("color: %1;").arg(colour(Colour::Error).name()); }

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

private:
    Theme() = default;
    void apply();
    void applySheet(); // the mode's own sheet, then the snippets
    static void releasePalettes(); // every widget follows the application's palette again

    QString m_snippetSheet;
    QFont m_systemFont;
    Mode m_mode = Mode::System;
    bool m_initialised = false;
    QString m_systemStyle;    // the desktop's style and palette, restored by System
    QPalette m_systemPalette;
    QStringList m_snippets;
};

} // namespace coeus
