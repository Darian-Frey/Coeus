#include "ui/WriteToast.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>

namespace coeus {

WriteToast::WriteToast(const QString& path, const std::optional<QByteArray>& before, const QByteArray& afterHash,
                       const QString& summary, QWidget* parent)
    : QWidget(parent)
    , m_path(path)
    , m_before(before)
    , m_afterHash(afterHash)
    , m_label(new QLabel(this))
    , m_undo(new QPushButton(tr("Undo"), this))
    , m_timer(new QTimer(this))
{
    setObjectName(QStringLiteral("writeToast"));
    setAttribute(Qt::WA_StyledBackground);
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::ToolTipBase);
    setForegroundRole(QPalette::ToolTipText);
    m_label->setText(summary);
    m_label->setWordWrap(true);
    m_label->setTextFormat(Qt::PlainText);
    m_label->setToolTip(path);
    auto* dismiss = new QToolButton(this);
    dismiss->setText(QStringLiteral("×"));
    dismiss->setAutoRaise(true);
    dismiss->setToolTip(tr("Keep the change"));
    connect(dismiss, &QToolButton::clicked, this, &QWidget::close);
    connect(m_undo, &QPushButton::clicked, this, [this] {
        m_timer->stop();
        m_undo->setEnabled(false);
        emit undoRequested(this);
    });
    m_timer->setSingleShot(true);
    m_timer->setInterval(kUndoMs);
    connect(m_timer, &QTimer::timeout, this, &QWidget::close);
    m_timer->start();
    setAttribute(Qt::WA_DeleteOnClose);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 6, 6, 6);
    layout->addWidget(m_label, 1);
    layout->addWidget(m_undo);
    layout->addWidget(dismiss);
    setMaximumWidth(460);
}

QString WriteToast::text() const
{
    return m_label->text();
}

void WriteToast::finish(const QString& outcome)
{
    m_label->setText(outcome);
    m_undo->hide();
    QTimer::singleShot(2500, this, &QWidget::close);
}

} // namespace coeus
