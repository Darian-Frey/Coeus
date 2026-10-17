#include "ui/VersionView.hpp"

#include "ui/Theme.hpp"

#include "ui/MarkdownHighlighter.hpp"

#include <QFileInfo>
#include <QFontDatabase>
#include <QLabel>
#include <QPlainTextEdit>
#include <QVBoxLayout>

namespace coeus {

VersionView::VersionView(const QString& notePath, const GitCommit& commit, const QString& text, QWidget* parent)
    : QWidget(parent)
    , m_notePath(notePath)
    , m_commit(commit)
    , m_header(new QLabel(this))
    , m_text(new QPlainTextEdit(this))
{
    m_header->setText(tr("%1 as of %2 — %3 · %4 · %5 (read-only)")
                          .arg(commit.path, commit.shortId, commit.summary,
                               commit.time.toString(QStringLiteral("yyyy-MM-dd HH:mm")), commit.author));
    m_header->setTextFormat(Qt::PlainText);
    m_header->setWordWrap(true);
    m_header->setForegroundRole(QPalette::PlaceholderText);
    m_header->setContentsMargins(6, 4, 6, 4);
    m_header->setToolTip(commit.message);

    m_text->setReadOnly(true);
    Theme::followTextZoom(m_text, true); // fixed pitch, at the text zoom
    m_text->setPlainText(text);
    new MarkdownHighlighter(m_text->document());

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_header);
    layout->addWidget(m_text, 1);
}

QString VersionView::title() const
{
    return QStringLiteral("%1 @ %2").arg(QFileInfo(m_commit.path).completeBaseName(), m_commit.shortId);
}

} // namespace coeus
