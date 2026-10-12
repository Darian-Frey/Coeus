#pragma once

#include <QWidget>

class QVBoxLayout;

namespace coeus {

// Right dock: a vertical stack of titled sections (backlinks, outline,
// properties, and later related notes, problems, Git history).
class SidebarPanel : public QWidget {
    Q_OBJECT

public:
    explicit SidebarPanel(QWidget* parent = nullptr);

    // Takes ownership of `body` through Qt parenting.
    void addSection(const QString& title, QWidget* body);

private:
    QVBoxLayout* m_sections = nullptr;
};

} // namespace coeus
