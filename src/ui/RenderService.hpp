#pragma once

#include "render/MermaidRenderer.hpp"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QSize>
#include <QString>

#include <expected>
#include <functional>

class QThreadPool;

namespace coeus {

// Pictures for reading mode that take a program to draw (D-022, SPEC.md
// §Rendered blocks): maths, Mermaid diagrams and PDF pages. Asked on the UI
// thread, it answers from `<vault>/.coeus/render/` at once, or queues the
// drawing on a pool of its own and says rendered() when it lands, so a view
// shows the code meanwhile and never waits (AV-005). A failure is kept too,
// so a bad formula is not drawn again on every redraw; the cache is derived
// data, never synced, safe to delete.
class RenderService : public QObject {
    Q_OBJECT

public:
    struct Picture {
        QString url;   // file: URL of the PNG; empty while drawing, or if it cannot be drawn
        QSize size;    // to show it at
        QString error; // why it could not be drawn; empty otherwise
    };

    explicit RenderService(QObject* parent = nullptr);
    ~RenderService() override;

    // The vault's cache folder; nothing is drawn without one.
    void setVault(const QString& vaultPath);
    QString cacheFolder() const { return m_folder; }

    Picture maths(const QString& tex, bool display);
    Picture mermaid(const QString& source);
    Picture pdfPage(const QString& pdfPath, int page);

    // Waits for every queued drawing; for tests and closing.
    void waitForDone();

signals:
    // A picture landed: views showing what was asked for should draw again.
    void rendered();
    // A tool is missing on this machine; said once per tool and vault.
    void unavailable(const QString& why);

private:
    Picture lookup(const QString& kind, const QByteArray& keySource, const std::function<std::expected<QSize, QString>(const QString& png)>& draw);
    QString scriptPath();
    MermaidRenderer::Style mermaidStyle() const;

    QString m_folder;
    QThreadPool* m_pool = nullptr;
    QSet<QString> m_pending; // keys being drawn
    QSet<QString> m_said;    // tools whose absence has been reported
};

} // namespace coeus
