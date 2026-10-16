#include "ui/GraphPanel.hpp"

#include "ui/GraphView.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace coeus {

GraphPanel::GraphPanel(Scope scope, QWidget* parent)
    : QWidget(parent)
    , m_scope(scope)
    , m_view(new GraphView(this))
    , m_status(new QLabel(this))
    , m_colouring(new QComboBox(this))
    , m_tags(new QCheckBox(tr("Tags"), this))
    , m_fit(new QToolButton(this))
{
    const bool global = scope == Scope::Global;
    QSettings settings;
    m_colouring->addItem(tr("By project"), int(GraphView::Colouring::Project));
    m_colouring->addItem(tr("By tag"), int(GraphView::Colouring::Tag));
    m_colouring->setToolTip(tr("Colour notes by their project or their first tag"));
    m_colouring->setCurrentIndex(settings.value(settingsKey("colouring"), 0).toInt() == 1 ? 1 : 0);
    m_view->setColouring(GraphView::Colouring(m_colouring->currentData().toInt()));
    m_tags->setToolTip(tr("Show tags as nodes joined to their notes"));
    m_tags->setChecked(settings.value(settingsKey("tags"), true).toBool());
    m_fit->setText(tr("Fit"));
    m_fit->setToolTip(tr("Fit the graph to the view (or double-click the background)"));
    m_status->setForegroundRole(QPalette::PlaceholderText);

    auto* controls = new QHBoxLayout;
    controls->setContentsMargins(global ? 6 : 0, global ? 4 : 0, global ? 6 : 0, 0);
    controls->addWidget(m_colouring);
    controls->addWidget(m_tags);
    if (global) {
        m_orphans = new QCheckBox(tr("Orphans"), this);
        m_orphans->setToolTip(tr("Show notes with no links"));
        m_orphans->setChecked(settings.value(settingsKey("orphans"), false).toBool());
        m_unresolved = new QCheckBox(tr("Unresolved"), this);
        m_unresolved->setToolTip(tr("Show links to notes that do not exist yet"));
        m_unresolved->setChecked(settings.value(settingsKey("unresolved"), false).toBool());
        m_linkedRoots = new QCheckBox(tr("Linked roots"), this);
        m_linkedRoots->setToolTip(tr("Show the notes of linked project repositories too"));
        m_linkedRoots->setChecked(settings.value(settingsKey("linkedRoots"), false).toBool());
        for (QCheckBox* box : {m_orphans, m_unresolved, m_linkedRoots}) {
            controls->addWidget(box);
            connect(box, &QCheckBox::toggled, this, [this] {
                saveChoices();
                refresh();
            });
        }
    } else {
        m_depth = new QSpinBox(this);
        m_depth->setRange(1, kMaxDepth);
        m_depth->setPrefix(tr("Depth "));
        m_depth->setToolTip(tr("How many links away from the note to show"));
        m_depth->setValue(std::clamp(settings.value(settingsKey("depth"), 1).toInt(), 1, kMaxDepth));
        controls->addWidget(m_depth);
        connect(m_depth, &QSpinBox::valueChanged, this, [this] {
            saveChoices();
            refresh();
        });
    }
    controls->addStretch();
    if (global)
        controls->addWidget(m_status);
    controls->addWidget(m_fit);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addLayout(controls);
    if (!global)
        layout->addWidget(m_status);
    layout->addWidget(m_view, 1);
    if (!global)
        setMinimumHeight(240);

    connect(m_colouring, &QComboBox::currentIndexChanged, this, [this] {
        m_view->setColouring(GraphView::Colouring(m_colouring->currentData().toInt()));
        saveChoices();
    });
    connect(m_tags, &QCheckBox::toggled, this, [this] {
        saveChoices();
        refresh();
    });
    connect(m_fit, &QToolButton::clicked, m_view, [this] {
        m_view->fit();
    });
    connect(m_view, &GraphView::noteActivated, this, &GraphPanel::openRequested);
    connect(m_view, &GraphView::tagActivated, this, &GraphPanel::tagRequested);
    refresh();
}

QString GraphPanel::settingsKey(const char* name) const
{
    return QStringLiteral("graph/%1/%2").arg(m_scope == Scope::Global ? QStringLiteral("global") : QStringLiteral("local"),
                                             QLatin1String(name));
}

void GraphPanel::saveChoices() const
{
    QSettings settings;
    settings.setValue(settingsKey("colouring"), m_colouring->currentIndex());
    settings.setValue(settingsKey("tags"), m_tags->isChecked());
    if (m_scope == Scope::Global) {
        settings.setValue(settingsKey("orphans"), m_orphans->isChecked());
        settings.setValue(settingsKey("unresolved"), m_unresolved->isChecked());
        settings.setValue(settingsKey("linkedRoots"), m_linkedRoots->isChecked());
    } else {
        settings.setValue(settingsKey("depth"), m_depth->value());
    }
}

LinkGraph::Options GraphPanel::options() const
{
    LinkGraph::Options options;
    options.tags = m_tags->isChecked();
    if (m_scope == Scope::Global) {
        options.orphans = m_orphans->isChecked();
        options.unresolved = m_unresolved->isChecked();
        options.linkedRoots = m_linkedRoots->isChecked();
    } else {
        // The note's neighbours wherever they are; the note itself even if alone.
        options.orphans = true;
        options.linkedRoots = true;
    }
    return options;
}

QString GraphPanel::status() const
{
    return m_status->text();
}

void GraphPanel::setIndexPath(const QString& indexPath)
{
    m_indexPath = indexPath;
    refresh();
}

void GraphPanel::showNote(const QString& absolutePath)
{
    if (absolutePath == m_notePath)
        return;
    m_notePath = absolutePath;
    m_view->setCurrentNote(absolutePath);
    if (m_scope == Scope::Local)
        refresh();
}

void GraphPanel::refresh()
{
    const int generation = ++m_generation;
    const bool local = m_scope == Scope::Local;
    if (m_indexPath.isEmpty() || (local && m_notePath.isEmpty())) {
        m_view->setGraph(LinkGraph());
        m_status->setText(local ? tr("Open a note to see what it links with.") : tr("Open a vault to see its graph."));
        return;
    }
    auto* watcher = new QFutureWatcher<std::expected<LinkGraph, QString>>(this);
    connect(watcher, &QFutureWatcher<std::expected<LinkGraph, QString>>::finished, this, [this, watcher, generation] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (generation == m_generation)
            present(result);
    });
    watcher->setFuture(QtConcurrent::run(
        [indexPath = m_indexPath, options = options(), note = local ? m_notePath : QString(), depth = local ? m_depth->value() : 0]()
            -> std::expected<LinkGraph, QString> {
            auto graph = LinkGraph::load(indexPath, options);
            if (!graph || note.isEmpty())
                return graph;
            return graph->neighbourhood(note, depth);
        }));
}

void GraphPanel::present(const std::expected<LinkGraph, QString>& graph)
{
    if (!graph) {
        m_view->setGraph(LinkGraph());
        m_status->setText(graph.error());
        return;
    }
    m_view->setGraph(*graph);
    const int notes = graph->noteCount();
    if (m_scope == Scope::Local) {
        m_status->setText(notes == 0 ? tr("Not indexed yet.")
                          : notes == 1 && graph->nodes().size() == 1 ? tr("No links in or out.")
                                                                     : tr("%n note(s) within reach", nullptr, notes));
    } else {
        m_status->setText(tr("%n note(s)", nullptr, notes) + QStringLiteral(" · ") + tr("%n connection(s)", nullptr, int(graph->edges().size())));
    }
    emit updated(int(graph->nodes().size()), int(graph->edges().size()));
}

} // namespace coeus
