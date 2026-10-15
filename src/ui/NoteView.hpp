#pragma once

#include "parser/HtmlRenderer.hpp"
#include "vault/Vault.hpp"

#include <QUrl>
#include <QPointer>
#include <QWidget>

class QLabel;
class QStackedWidget;
class QTextBrowser;
class QTimer;
class QToolButton;

namespace coeus {

class RenderService;

class Editor;
class PropertiesPanel;

// One open note: a breadcrumb header with the mode switch, and either the
// source Editor or the rendered reading view (F-003). Both show the same
// buffer; reading mode never changes it.
class NoteView : public QWidget {
    Q_OBJECT

public:
    // Source shows the Markdown, Live the Markdown as it reads (F-003),
    // Reading the rendered note; Source and Live edit the same buffer.
    enum class Mode { Source, Live, Reading };

    NoteView(const Vault& root, const QString& absolutePath, const QString& displayPath, QWidget* parent = nullptr);

    Editor* editor() const { return m_editor; }
    PropertiesPanel* properties() const { return m_properties; }
    QTextBrowser* reader() const { return m_reader; }
    QString path() const { return m_path; }
    // The index embeds are resolved through (F-023); without it they show as links.
    void setIndexPath(const QString& indexPath) { m_indexPath = indexPath; }
    // Draws maths, Mermaid and PDF pages (D-022); without it they stay code and cards.
    void setRenderService(RenderService* service);
    // Renders the reading view again if it is showing, e.g. an embedded note changed.
    void refreshReading();
    // The ☆ in the header (F-014): filled while the note is bookmarked.
    void setStarred(bool starred);
    QToolButton* starButton() const { return m_starButton; }

    Mode mode() const { return m_mode; }
    void setMode(Mode mode);
    // Reading, or back to the editing mode last used.
    void toggleMode();
    // The editor, in the editing mode last used, if reading mode is showing.
    void showEditor();
    QToolButton* liveButton() const { return m_liveButton; }

    // Renders the buffer into the reading view now.
    void render();

    // Brings a heading to the top of the view, in either mode (F-013).
    // `line` is 1-based in the buffer; in reading mode the heading is found
    // by its text and occurrence, falling back to its position `index`.
    void goToHeading(int line, const QString& text, int occurrence, int index);

    // Delay before the reading view follows a changed buffer.
    static constexpr int kRenderDelayMs = 250;

signals:
    // A link in the reading view was clicked (scheme as HtmlRenderer documents).
    void linkActivated(const QUrl& url);
    void modeChanged(Mode mode);
    // The ☆ was clicked: star or unstar this note.
    void starToggled();

private:
    QString resolveImage(const QString& target, bool wiki) const;
    HtmlRenderer::Embedded resolveNote(const QString& target, const QString& fromPath) const;
    QString styleSheet() const;

    Vault m_root;
    QString m_indexPath;
    QPointer<RenderService> m_render;
    QString m_path;
    Mode m_mode = Mode::Source;
    Editor* m_editor = nullptr;
    PropertiesPanel* m_properties = nullptr;
    QTextBrowser* m_reader = nullptr;
    QStackedWidget* m_stack = nullptr;
    QToolButton* m_starButton = nullptr;
    QToolButton* m_sourceButton = nullptr;
    QToolButton* m_liveButton = nullptr;
    Mode m_editMode = Mode::Source; // the editing mode Ctrl+E returns to
    QToolButton* m_readingButton = nullptr;
    QTimer* m_renderTimer = nullptr;
};

} // namespace coeus
