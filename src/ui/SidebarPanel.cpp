#include "ui/SidebarPanel.hpp"

#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

namespace coeus {

SidebarPanel::SidebarPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* content = new QWidget;
    m_sections = new QVBoxLayout(content);
    m_sections->addStretch();

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(scroll);
}

void SidebarPanel::addSection(const QString& title, QWidget* body)
{
    auto* heading = new QLabel(title.toUpper());
    QFont font = heading->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() * 0.85);
    heading->setFont(font);
    heading->setForegroundRole(QPalette::PlaceholderText);

    // Insert above the trailing stretch.
    const int at = m_sections->count() - 1;
    m_sections->insertWidget(at, heading);
    m_sections->insertWidget(at + 1, body);
    m_sections->insertSpacing(at + 2, 12);
}

} // namespace coeus
