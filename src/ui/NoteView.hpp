#pragma once

#include "vault/Vault.hpp"

#include <QUrl>
#include <QWidget>

class QLabel;
class QStackedWidget;
class QTextBrowser;
class QTimer;
class QToolButton;

namespace coeus {

class Editor;

// One open note: a breadcrumb header with the mode switch, and either the
// source Editor or the rendered reading view (F-003). Both show the same
// buffer; reading mode never changes it.
class NoteView : public QWidget {
    Q_OBJECT

public:
    enum class Mode { Source, Reading };

    NoteView(const Vault& root, const QString& absolutePath, const QString& displayPath, QWidget* parent = nullptr);

    Editor* editor() const { return m_editor; }
    QTextBrowser* reader() const { return m_reader; }
    QString path() const { return m_path; }

    Mode mode() const { return m_mode; }
    void setMode(Mode mode);
    void toggleMode();

    // Renders the buffer into the reading view now.
    void render();

    // Delay before the reading view follows a changed buffer.
    static constexpr int kRenderDelayMs = 250;

signals:
    // A link in the reading view was clicked (scheme as HtmlRenderer documents).
    void linkActivated(const QUrl& url);
    void modeChanged(Mode mode);

private:
    QString resolveImage(const QString& target, bool wiki) const;
    QString styleSheet() const;

    Vault m_root;
    QString m_path;
    Mode m_mode = Mode::Source;
    Editor* m_editor = nullptr;
    QTextBrowser* m_reader = nullptr;
    QStackedWidget* m_stack = nullptr;
    QToolButton* m_sourceButton = nullptr;
    QToolButton* m_readingButton = nullptr;
    QTimer* m_renderTimer = nullptr;
};

} // namespace coeus
