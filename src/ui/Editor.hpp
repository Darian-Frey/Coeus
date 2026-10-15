#pragma once

#include "git/GitRepository.hpp"
#include "index/IndexQueries.hpp"
#include "vault/Vault.hpp"

#include <QByteArray>
#include <QPlainTextEdit>
#include <QUrl>
#include <QTimer>
#include <QString>

#include <expected>

namespace coeus {

class LivePreview;

class BlameGutter;
class MarkdownHighlighter;
class NoteCatalogue;
class WikilinkCompleter;

// A note open in a tab, in source mode (F-003). Remembers the content hash of
// the version on disk it was loaded from or last saved as, so a save never
// overwrites a file someone else changed meanwhile (AV-002): that case is a
// conflict for the author to resolve, never a silent overwrite.
class Editor : public QPlainTextEdit {
    Q_OBJECT

public:
    // Live preview (F-003): the text styled as it reads, its syntax hidden
    // away from the cursor, pictures drawn in place. Off is source mode.
    void setLive(bool live);
    bool isLive() const;
    LivePreview* live() const { return m_live; }
    // Background tints live preview adds (code blocks), kept with the lint's underlines.
    void setLiveSelections(const QList<QTextEdit::ExtraSelection>& selections);
    // A line's rectangle in the viewport's coordinates.
    QRectF blockRect(const QTextBlock& block) const;
    // The link at `position` as a URL in HtmlRenderer's schemes (coeus-wiki:,
    // coeus-file:, or the address), or empty if there is none.
    QUrl linkAt(int position) const;

    // The ID of the paragraph or list item at the cursor (F-024), giving it
    // a new one at the end of its last line, as one undo step, if it has none;
    // empty if the cursor is not in such a block. `added` says which.
    QString ensureBlockId(bool* added = nullptr);
    // Commands added to the editor's context menu, after its own.
    void setContextActions(const QList<QAction*>& actions) { m_contextActions = actions; }

    enum class SaveResult { Saved, Conflict, Failed };
    enum class DiskState { Unchanged, Reloaded, Conflict, Deleted };

    Editor(const Vault& root, const QString& absolutePath, QWidget* parent = nullptr);

    // Reads the note from disk, replacing the buffer.
    std::expected<void, QString> load();
    // Puts recovered text in the buffer as unsaved changes on top of the disk
    // version that had hash `baseHash` (Journal).
    void restore(const QString& text, const QByteArray& baseHash);

    // Saves unless the file changed on disk since it was loaded (Conflict).
    SaveResult save(QString* error = nullptr);
    // Compares the disk with the loaded version after an external change
    // (F-011): reloads an unmodified buffer, reports a conflict otherwise.
    DiskState checkDisk();

    // Conflict resolutions.
    void keepMine();             // the next save overwrites the disk version
    std::expected<void, QString> takeDisk(); // discard the buffer, load the disk version

    QString path() const { return m_path; }
    QByteArray baseHash() const { return m_baseHash; }
    bool isModified() const;
    bool inConflict() const { return m_conflict; }
    MarkdownHighlighter* highlighter() const { return m_highlighter; }

    // Turns on [[ completion over the catalogue's notes (F-004).
    void setCatalogue(const NoteCatalogue* catalogue);
    WikilinkCompleter* completer() const { return m_completer; }

    // Lint problems in this note (F-016), underlined with a wavy line in the
    // level's colour and shown as tooltips. Each is found by its subject on
    // its line; one whose line no longer holds it (the buffer has moved on
    // since the index read the file) is not shown. Kept across reloads, and
    // refreshed from the catalogue whenever it reloads.
    void setProblems(const QList<ProblemRef>& problems);
    // The problem text under `position` in the document, or empty.
    QString problemAt(int position) const;

    // The blame gutter (F-021). showBlame() opens it and asks for blame
    // (blameRequested); setBlame() fills it, one entry per line of the text it
    // was computed for. While it is open, an edit asks again once typing pauses.
    void showBlame();
    void hideBlame();
    bool blameShown() const { return m_blameOn; }
    void setBlame(const QList<BlameLine>& lines);
    const QList<BlameLine>& blame() const { return m_blame; }
    // What the gutter shows beside block `blockNumber`: "a41c9e2 2026-09-25"
    // at the start of a run of lines from one commit, "" within it,
    // "Not committed" for new lines; with `always`, within a run too.
    QString blameLabel(int blockNumber, bool always = false) const;
    QString blameToolTip(int blockNumber) const;
    int blameGutterWidth() const;
    void paintBlame(QPaintEvent* event);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    bool viewportEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

signals:
    void modificationChanged(bool modified);
    void blameRequested();
    void blameShownChanged(bool shown);
    // Ctrl+click on a link (F-003): `url` as linkAt() gives it.
    void linkActivated(const QUrl& url);
    void conflictChanged(bool conflict);

private:
    std::expected<QByteArray, QString> diskBytes() const;
    QByteArray currentHashOnDisk() const; // empty if the file is gone or unreadable
    void setBuffer(const QString& text);
    void setConflict(bool conflict);
    void applyProblems();

    Vault m_root;
    QString m_path;
    QByteArray m_baseHash; // ContentHash of the disk version the buffer is based on; empty if none
    bool m_conflict = false;
    MarkdownHighlighter* m_highlighter = nullptr;
    WikilinkCompleter* m_completer = nullptr;
    QList<ProblemRef> m_problems;
    QList<QAction*> m_contextActions;
    QList<QTextEdit::ExtraSelection> m_problemSelections;
    QList<QTextEdit::ExtraSelection> m_liveSelections;
    LivePreview* m_live = nullptr;
    void showSelections();
    void updateLinkCursor(Qt::KeyboardModifiers modifiers, const QPoint& at);
    bool m_blameOn = false;
    QList<BlameLine> m_blame;
    BlameGutter* m_blameGutter = nullptr;
    QTimer* m_blameTimer = nullptr;
    QMetaObject::Connection m_catalogueReloaded;
};

} // namespace coeus
