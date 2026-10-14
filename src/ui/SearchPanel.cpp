#include "ui/SearchPanel.hpp"

#include "index/Database.hpp"
#include "search/Embedder.hpp"
#include "search/VectorStore.hpp"

#include <QComboBox>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QHBoxLayout>
#include <QFutureWatcher>
#include <QLabel>
#include <QLineEdit>
#include <QThreadPool>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <mutex>

namespace coeus {

namespace {

constexpr int kPathRole = Qt::UserRole + 1;
constexpr int kLineRole = Qt::UserRole + 2;
constexpr int kMeaningLimit = 50;

// The snippet with its match markers turned into bold, the rest escaped.
QString snippetHtml(const QString& snippet)
{
    QString html;
    QString run;
    for (const QChar c : snippet) {
        if (c == Search::kMarkStart || c == Search::kMarkEnd) {
            html += run.toHtmlEscaped();
            run.clear();
            html += c == Search::kMarkStart ? QStringLiteral("<b>") : QStringLiteral("</b>");
        } else {
            run += c;
        }
    }
    return html + run.toHtmlEscaped();
}

QString plain(const QString& text)
{
    return text.toHtmlEscaped();
}

} // namespace

struct SearchPanel::Model {
    std::mutex mutex;
    QString path;
    QString loadedFrom;
    std::unique_ptr<Embedder> embedder;
};

SearchPanel::SearchPanel(QWidget* parent)
    : QWidget(parent)
    , m_input(new QLineEdit(this))
    , m_status(new QLabel(this))
    , m_results(new QTreeWidget(this))
    , m_typingTimer(new QTimer(this))
{
    m_model = std::make_shared<Model>();
    m_model->path = Embedder::defaultModelPath();
    m_modelPool = new QThreadPool(this);
    m_modelPool->setMaxThreadCount(1); // the embedder is used by one thread at a time
    m_mode = new QComboBox(this);
    m_mode->addItem(tr("Words"), int(Mode::Words));
    m_mode->addItem(tr("Meaning"), int(Mode::Meaning));
    m_mode->addItem(tr("Both"), int(Mode::Both));
    m_mode->setToolTip(tr("Words: the text as typed. Meaning: passages about the same thing, in other words. Both: the two "
                          "merged, notes found both ways first."));
    connect(m_mode, &QComboBox::currentIndexChanged, this, &SearchPanel::refresh);
    m_input->setPlaceholderText(tr("Search…   \"phrase\"  -word  tag:  path:  file:  re:/…/"));
    m_input->setClearButtonEnabled(true);
    m_status->setForegroundRole(QPalette::PlaceholderText);
    m_status->setWordWrap(true);
    m_status->setMargin(4);

    m_results->setHeaderHidden(true);
    m_results->setRootIsDecorated(false);
    m_results->setFrameShape(QFrame::NoFrame);
    m_results->setIndentation(0);
    auto open = [this](QTreeWidgetItem* item) {
        QTreeWidgetItem* top = item->parent() ? item->parent() : item;
        const int line = top->data(0, kLineRole).toInt();
        if (line > 0)
            emit openAtLineRequested(top->data(0, kPathRole).toString(), line);
        else
            emit openRequested(top->data(0, kPathRole).toString(), m_words);
    };
    connect(m_results, &QTreeWidget::itemClicked, this, open);
    connect(m_results, &QTreeWidget::itemActivated, this, open);

    m_typingTimer->setSingleShot(true);
    m_typingTimer->setInterval(kTypingDelayMs);
    connect(m_typingTimer, &QTimer::timeout, this, &SearchPanel::run);
    connect(m_input, &QLineEdit::textChanged, m_typingTimer, qOverload<>(&QTimer::start));
    connect(m_input, &QLineEdit::returnPressed, this, &SearchPanel::run);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(m_input, 1);
    row->addWidget(m_mode);
    layout->addLayout(row);
    layout->addWidget(m_status);
    layout->addWidget(m_results, 1);
    present(Outcome{QList<Row>(), QString(), 0});
}

SearchPanel::~SearchPanel()
{
    m_modelPool->waitForDone(); // the embedder is not freed under a running query
}

void SearchPanel::setModelPath(const QString& modelPath)
{
    std::lock_guard lock(m_model->mutex);
    m_model->path = modelPath;
}

void SearchPanel::setMode(Mode mode)
{
    m_mode->setCurrentIndex(m_mode->findData(int(mode)));
}

SearchPanel::Mode SearchPanel::mode() const
{
    return Mode(m_mode->currentData().toInt());
}

void SearchPanel::setIndexPath(const QString& indexPath)
{
    m_indexPath = indexPath;
    ++m_generation;
    m_results->clear();
}

void SearchPanel::refresh()
{
    if (!m_input->text().trimmed().isEmpty())
        run();
}

void SearchPanel::setQuery(const QString& text)
{
    m_input->setText(text);
    m_typingTimer->stop();
    run();
}

void SearchPanel::run()
{
    m_typingTimer->stop();
    const SearchQuery query = SearchQuery::parse(m_input->text());
    const int generation = ++m_generation;
    if (query.isEmpty() && query.error.isEmpty()) {
        m_words.clear();
        present(Outcome{QList<Row>(), QString(), 0});
        return;
    }
    auto* watcher = new QFutureWatcher<Outcome>(this);
    connect(watcher, &QFutureWatcher<Outcome>::finished, this, [this, watcher, generation, query] {
        watcher->deleteLater();
        if (generation != m_generation)
            return; // the query changed meanwhile
        m_words = query.highlightWords();
        present(watcher->result());
    });
    const Mode mode = this->mode();
    const auto words = [index = m_indexPath, query] {
        QList<Row> rows;
        const auto hits = Search::run(index, query);
        if (!hits)
            return std::expected<QList<Row>, QString>(std::unexpected(hits.error()));
        for (const Search::Hit& hit : *hits)
            rows.append({hit.notePath, hit.noteDisplay, snippetHtml(hit.snippet), 0});
        return std::expected<QList<Row>, QString>(rows);
    };
    if (mode == Mode::Words) {
        watcher->setFuture(QtConcurrent::run([words] {
            QElapsedTimer timer;
            timer.start();
            Outcome outcome{words(), QString(), 0};
            outcome.elapsedMs = timer.elapsed();
            return outcome;
        }));
        return;
    }
    // By meaning: the text as typed is the question, operators and all.
    watcher->setFuture(QtConcurrent::run(m_modelPool, [model = m_model, index = m_indexPath, text = m_input->text().trimmed(),
                                                       mode, words] {
        QElapsedTimer timer;
        timer.start();
        Outcome outcome{QList<Row>(), QString(), 0};
        std::lock_guard lock(model->mutex);
        if (!model->embedder || model->loadedFrom != model->path) {
            auto loaded = Embedder::load(model->path);
            if (!loaded) {
                outcome.rows = std::unexpected(QStringLiteral("Search by meaning needs the embedding model. %1").arg(loaded.error()));
                return outcome;
            }
            model->embedder = std::move(*loaded);
            model->loadedFrom = model->path;
        }
        const auto vector = model->embedder->embed({text}, Embedder::Purpose::Query);
        if (!vector) {
            outcome.rows = std::unexpected(vector.error());
            return outcome;
        }
        const auto meaning = SemanticSearch::nearest(index, vector->front(), kMeaningLimit);
        if (!meaning) {
            outcome.rows = std::unexpected(meaning.error());
            return outcome;
        }
        QList<Row> rows;
        if (mode == Mode::Meaning) {
            for (const SemanticSearch::Hit& hit : *meaning)
                rows.append({hit.notePath, hit.heading.isEmpty() ? hit.display : hit.display + QStringLiteral(" › ") + hit.heading,
                             plain(hit.snippet), std::max(1, hit.startLine)});
        } else {
            QList<QPair<QString, QPair<QString, QString>>> byWords;
            QHash<QString, QString> display;
            if (const auto found = words()) {
                for (const Row& row : *found) {
                    byWords.append({row.notePath, {row.title, row.detail}});
                    display.insert(row.notePath, row.title);
                }
            }
            QList<SemanticSearch::Hit> escaped = *meaning;
            for (SemanticSearch::Hit& hit : escaped) {
                hit.snippet = plain(hit.snippet);
                display.insert(hit.notePath, display.value(hit.notePath, hit.display));
            }
            for (const SemanticSearch::Blended& hit : SemanticSearch::blend(byWords, escaped, kMeaningLimit)) {
                const QString title = display.value(hit.notePath, hit.title);
                rows.append({hit.notePath, hit.byWords ? title : (hit.heading.isEmpty() ? title : title + QStringLiteral(" › ") + hit.heading),
                             hit.snippet, hit.byWords ? 0 : std::max(1, hit.line)});
            }
        }
        outcome.rows = rows;
        if (auto db = Database::open(index, Database::Mode::ReadOnly)) {
            if (const auto coverage = VectorStore::coverage(*db); coverage && !coverage->complete())
                outcome.note = QObject::tr("%L1 of %L2 passages embedded so far; the rest are on their way")
                                   .arg(coverage->embedded)
                                   .arg(coverage->total);
        }
        outcome.elapsedMs = timer.elapsed();
        return outcome;
    }));
}

void SearchPanel::present(const Outcome& outcome)
{
    m_results->clear();
    if (!outcome.rows) {
        m_status->setText(outcome.rows.error());
        emit finished(0);
        return;
    }
    for (const Row& row : *outcome.rows) {
        auto* item = new QTreeWidgetItem(m_results, {row.title});
        item->setData(0, kPathRole, row.notePath);
        item->setData(0, kLineRole, row.line);
        item->setToolTip(0, row.notePath);
        QFont bold = item->font(0);
        bold.setBold(true);
        item->setFont(0, bold);
        if (!row.detail.isEmpty()) {
            auto* line = new QTreeWidgetItem(item);
            auto* label = new QLabel(row.detail);
            label->setWordWrap(true);
            label->setTextFormat(Qt::RichText);
            label->setForegroundRole(QPalette::PlaceholderText);
            m_results->setItemWidget(line, 0, label);
        }
        item->setExpanded(true);
    }
    if (m_input->text().trimmed().isEmpty()) {
        m_status->setText(tr("Type to search every note."));
    } else {
        QString text = tr("%n result(s) in %1 ms", nullptr, int(outcome.rows->size())).arg(outcome.elapsedMs);
        if (!outcome.note.isEmpty())
            text += QStringLiteral(" · ") + outcome.note;
        m_status->setText(text);
    }
    emit finished(int(outcome.rows->size()));
}

} // namespace coeus
