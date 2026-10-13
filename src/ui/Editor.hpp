#pragma once

#include "index/IndexQueries.hpp"
#include "vault/Vault.hpp"

#include <QByteArray>
#include <QPlainTextEdit>
#include <QString>

#include <expected>

namespace coeus {

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

protected:
    void keyPressEvent(QKeyEvent* event) override;
    bool viewportEvent(QEvent* event) override;

signals:
    void modificationChanged(bool modified);
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
    QMetaObject::Connection m_catalogueReloaded;
};

} // namespace coeus
