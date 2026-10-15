#pragma once

#include "parser/LiveBlocks.hpp"

#include <QHash>
#include <QImage>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QSize>
#include <QTextEdit>

#include <functional>
#include <optional>

class QPainter;
class QTimer;

namespace coeus {

class Editor;
class RenderService;

// Live preview (F-003, SPEC.md §Live preview) for one Editor: which lines the
// cursor reveals, and what the rest becomes. Frontmatter and a fence's ```
// lines fold away, code is tinted, and a picture (an image, a PDF page, $$
// maths, a ```mermaid diagram) is drawn over a line made as tall as it, the
// construct's other lines folded. Everything stays the buffer's text: the
// folding is the layout's, the hiding the highlighter's, nothing is written.
class LivePreview : public QObject {
    Q_OBJECT

public:
    explicit LivePreview(Editor* editor);

    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }
    void setRenderService(RenderService* service);
    // Maps an image target, as an embed or Markdown image writes it, to a
    // file: URL, or empty if it cannot be found (NoteView's resolver).
    void setImageResolver(std::function<QString(const QString& target, bool wiki)> resolver);

    // Draws the pictures over their lines; Editor's paintEvent calls it.
    void paint(QPainter& painter, const QRect& exposed);

    const QList<LiveBlock>& blocks() const { return m_blocks; }
    // The block numbers revealed: the cursor's and the selection's lines.
    QPair<int, int> revealed() const { return {m_revealFirst, m_revealLast}; }
    // A picture block's drawn size, if it is drawn now.
    QSize pictureSize(int firstLine) const { return m_drawn.value(firstLine).size; }

    static constexpr int kScanDelayMs = 120;
    static constexpr int kPictureMargin = 6;

public slots:
    // Recomputes what is shown; after typing, a cursor move or a picture landing.
    void update();

private:
    struct Drawn {
        QImage image;
        QSize size; // shown at
    };
    void rescan();
    std::optional<Drawn> pictureFor(const LiveBlock& block);
    std::optional<Drawn> draw(const LiveBlock& block, int room);
    QImage load(const QString& path);
    bool closesFence(int line) const;

    Editor* m_editor;
    bool m_enabled = false;
    QPointer<RenderService> m_render;
    std::function<QString(const QString&, bool)> m_resolve;
    QList<LiveBlock> m_blocks;
    QTimer* m_scanTimer = nullptr;
    int m_revealFirst = -1;
    int m_revealLast = -1;
    QSet<int> m_folded;              // lines hidden now
    QHash<int, Drawn> m_drawn;       // picture blocks drawn now, by first line
    QHash<QString, QImage> m_images; // loaded pictures, by path and modification time
    QHash<QString, std::optional<Drawn>> m_pictures; // by kind, source and room; cleared when a render lands
};

} // namespace coeus
