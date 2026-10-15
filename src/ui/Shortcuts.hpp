#pragma once

#include <QKeySequence>
#include <QList>
#include <QString>

class QAction;

namespace coeus {

// Rebindable keyboard shortcuts (F-027, SPEC.md §Keyboard shortcuts). Every
// command registered with MainWindow::registerAction() keeps its built-in
// shortcut as its default; the author's choices are saved in Coeus's
// settings under [shortcuts], one key per command, and applied as each
// command is registered. A shortcut is never given to two commands.
class Shortcuts {
public:
    // The command's settings key: its object name if it has one, else its
    // menu text without mnemonics or the trailing "…".
    static QString idOf(const QAction* action);
    // Records `action`'s built-in shortcut as its default, once, then applies
    // the saved choice, if any.
    static void adopt(QAction* action);
    static QKeySequence defaultOf(const QAction* action);
    // The command in `actions`, other than `action`, that already has
    // `sequence`; null if none or if `sequence` is empty.
    static QAction* conflict(const QList<QAction*>& actions, const QAction* action, const QKeySequence& sequence);
    // Gives `action` `sequence` (empty for none) and saves it; a sequence
    // equal to the default removes the saved choice.
    static void set(QAction* action, const QKeySequence& sequence);
    static void reset(QAction* action);
};

} // namespace coeus
