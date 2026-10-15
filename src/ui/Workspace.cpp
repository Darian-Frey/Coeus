#include "ui/Workspace.hpp"

#include <QApplication>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

namespace coeus {

Workspace::Workspace(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    QTabWidget* first = newPane();
    layout->addWidget(first);
    makeCurrent(first);
    // The pane holding the focus is the current one.
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) {
        for (QWidget* w = now; w; w = w->parentWidget()) {
            if (auto* pane = qobject_cast<QTabWidget*>(w); pane && m_panes.contains(pane)) {
                if (pane != m_current)
                    makeCurrent(pane);
                return;
            }
            if (w == this)
                return;
        }
    });
}

QTabWidget* Workspace::newPane()
{
    auto* pane = new QTabWidget;
    pane->setDocumentMode(true);
    pane->setTabsClosable(true);
    pane->setMovable(true);
    connect(pane, &QTabWidget::currentChanged, this, [this, pane] {
        if (pane == m_current)
            emit currentChanged(currentIndex());
    });
    connect(pane, &QTabWidget::tabBarClicked, this, [this, pane] { makeCurrent(pane); });
    connect(pane, &QTabWidget::tabCloseRequested, this, [this, pane](int local) { emit tabCloseRequested(globalIndex(pane, local)); });
    m_panes.append(pane);
    return pane;
}

void Workspace::makeCurrent(QTabWidget* pane)
{
    if (m_current == pane)
        return;
    m_current = pane;
    emit currentChanged(currentIndex());
}

int Workspace::globalIndex(const QTabWidget* pane, int local) const
{
    int base = 0;
    for (const QTabWidget* p : m_panes) {
        if (p == pane)
            return local < 0 ? -1 : base + local;
        base += p->count();
    }
    return -1;
}

QTabWidget* Workspace::paneAt(int index, int* local) const
{
    if (index < 0)
        return nullptr;
    for (QTabWidget* p : m_panes) {
        if (index < p->count()) {
            *local = index;
            return p;
        }
        index -= p->count();
    }
    return nullptr;
}

QTabWidget* Workspace::paneOf(QWidget* page) const
{
    for (QTabWidget* p : m_panes)
        if (p->indexOf(page) >= 0)
            return p;
    return nullptr;
}

int Workspace::count() const
{
    int total = 0;
    for (const QTabWidget* p : m_panes)
        total += p->count();
    return total;
}

QWidget* Workspace::widget(int index) const
{
    int local = 0;
    QTabWidget* pane = paneAt(index, &local);
    return pane ? pane->widget(local) : nullptr;
}

int Workspace::indexOf(QWidget* page) const
{
    for (const QTabWidget* p : m_panes)
        if (const int local = p->indexOf(page); local >= 0)
            return globalIndex(p, local);
    return -1;
}

QWidget* Workspace::currentWidget() const
{
    return m_current ? m_current->currentWidget() : nullptr;
}

int Workspace::currentIndex() const
{
    return m_current ? globalIndex(m_current, m_current->currentIndex()) : -1;
}

void Workspace::setCurrentIndex(int index)
{
    int local = 0;
    if (QTabWidget* pane = paneAt(index, &local)) {
        const bool samePane = pane == m_current;
        pane->setCurrentIndex(local);
        if (!samePane)
            makeCurrent(pane);
    }
}

void Workspace::setCurrentWidget(QWidget* page)
{
    setCurrentIndex(indexOf(page));
}

int Workspace::addTab(QWidget* page, const QString& title)
{
    QTabWidget* pane = m_current ? m_current.data() : m_panes.front();
    return globalIndex(pane, pane->addTab(page, title));
}

int Workspace::insertTab(int index, QWidget* page, const QString& title)
{
    int local = 0;
    QTabWidget* pane = paneAt(index, &local);
    if (!pane)
        return addTab(page, title);
    return globalIndex(pane, pane->insertTab(local, page, title));
}

void Workspace::removeTab(int index)
{
    int local = 0;
    if (QTabWidget* pane = paneAt(index, &local)) {
        pane->removeTab(local);
        removeIfEmpty(pane);
    }
}

QString Workspace::tabText(int index) const
{
    int local = 0;
    const QTabWidget* pane = paneAt(index, &local);
    return pane ? pane->tabText(local) : QString();
}

void Workspace::setTabText(int index, const QString& text)
{
    int local = 0;
    if (QTabWidget* pane = paneAt(index, &local))
        pane->setTabText(local, text);
}

QString Workspace::tabToolTip(int index) const
{
    int local = 0;
    const QTabWidget* pane = paneAt(index, &local);
    return pane ? pane->tabToolTip(local) : QString();
}

void Workspace::setTabToolTip(int index, const QString& tip)
{
    int local = 0;
    if (QTabWidget* pane = paneAt(index, &local))
        pane->setTabToolTip(local, tip);
}

void Workspace::removeIfEmpty(QTabWidget* pane)
{
    if (pane->count() > 0 || m_panes.size() < 2)
        return;
    // Often inside the pane's own signal (its last tab's close button): take
    // it out of the list now, and out of the layout later (Qt forbids deleting
    // a widget in its own signal).
    const qsizetype at = m_panes.indexOf(pane);
    m_panes.removeAll(pane);
    if (m_current == pane)
        makeCurrent(m_panes.value(std::max<qsizetype>(0, at - 1)));
    QMetaObject::invokeMethod(this, [this, pane] {
        auto* splitter = qobject_cast<QSplitter*>(pane->parentWidget());
        pane->hide();
        pane->setParent(nullptr);
        pane->deleteLater();
        // A splitter left with one child gives way to it.
        if (splitter && splitter->count() == 1) {
            QWidget* only = splitter->widget(0);
            if (auto* outer = qobject_cast<QSplitter*>(splitter->parentWidget())) {
                const int at = outer->indexOf(splitter);
                const QList<int> sizes = outer->sizes();
                outer->replaceWidget(at, only);
                outer->setSizes(sizes);
            } else {
                layout()->replaceWidget(splitter, only);
            }
            splitter->hide();
            splitter->deleteLater();
        }
    }, Qt::QueuedConnection);
}

void Workspace::split(Qt::Orientation orientation)
{
    QTabWidget* from = m_current ? m_current.data() : m_panes.front();
    // Its place: in a splitter of this orientation, or a new splitter there.
    QTabWidget* pane = newPane();
    m_panes.removeLast();
    m_panes.insert(m_panes.indexOf(from) + 1, pane);
    auto* parent = qobject_cast<QSplitter*>(from->parentWidget());
    if (parent && parent->orientation() == orientation) {
        const QList<int> sizes = parent->sizes();
        const int at = parent->indexOf(from);
        parent->insertWidget(at + 1, pane);
        QList<int> shared = sizes;
        shared[at] = sizes[at] / 2;
        shared.insert(at + 1, sizes[at] - sizes[at] / 2);
        parent->setSizes(shared);
    } else {
        auto* splitter = new QSplitter(orientation);
        splitter->setChildrenCollapsible(false);
        if (parent) {
            const QList<int> sizes = parent->sizes();
            const int at = parent->indexOf(from);
            parent->replaceWidget(at, splitter);
            parent->setSizes(sizes);
        } else {
            layout()->replaceWidget(from, splitter);
        }
        splitter->addWidget(from);
        splitter->addWidget(pane);
        splitter->setSizes({1, 1});
    }
    // The current tab goes with it, if its pane keeps another.
    if (from->count() > 1) {
        QWidget* page = from->currentWidget();
        const QString title = from->tabText(from->currentIndex());
        const QString tip = from->tabToolTip(from->currentIndex());
        from->removeTab(from->currentIndex());
        const int local = pane->addTab(page, title);
        pane->setTabToolTip(local, tip);
    }
    makeCurrent(pane);
    if (QWidget* page = pane->currentWidget())
        page->setFocus();
}

bool Workspace::moveToNextPane()
{
    if (m_panes.size() < 2 || !m_current || m_current->count() == 0)
        return false;
    QTabWidget* from = m_current;
    QTabWidget* to = m_panes[(m_panes.indexOf(from) + 1) % m_panes.size()];
    QWidget* page = from->currentWidget();
    const QString title = from->tabText(from->currentIndex());
    const QString tip = from->tabToolTip(from->currentIndex());
    from->removeTab(from->currentIndex());
    const int local = to->addTab(page, title);
    to->setTabToolTip(local, tip);
    to->setCurrentIndex(local);
    makeCurrent(to);
    page->setFocus();
    removeIfEmpty(from);
    return true;
}

void Workspace::focusNextPane()
{
    if (m_panes.size() < 2 || !m_current)
        return;
    QTabWidget* next = m_panes[(m_panes.indexOf(m_current) + 1) % m_panes.size()];
    makeCurrent(next);
    if (QWidget* page = next->currentWidget())
        page->setFocus();
}

} // namespace coeus
