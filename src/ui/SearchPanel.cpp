#include "ui/SearchPanel.hpp"

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace coeus {

namespace {

constexpr int kPathRole = Qt::UserRole + 1;

struct Outcome {
    std::expected<QList<Search::Hit>, QString> result;
    qint64 elapsedMs = 0;
};

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

} // namespace

SearchPanel::SearchPanel(QWidget* parent)
    : QWidget(parent)
    , m_input(new QLineEdit(this))
    , m_status(new QLabel(this))
    , m_results(new QTreeWidget(this))
    , m_typingTimer(new QTimer(this))
{
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
    layout->addWidget(m_input);
    layout->addWidget(m_status);
    layout->addWidget(m_results, 1);
    present(QList<Search::Hit>(), 0);
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
        present(QList<Search::Hit>(), 0);
        return;
    }
    auto* watcher = new QFutureWatcher<Outcome>(this);
    connect(watcher, &QFutureWatcher<Outcome>::finished, this, [this, watcher, generation, query] {
        watcher->deleteLater();
        if (generation != m_generation)
            return; // the query changed meanwhile
        m_words = query.highlightWords();
        const Outcome outcome = watcher->result();
        present(outcome.result, outcome.elapsedMs);
    });
    watcher->setFuture(QtConcurrent::run([index = m_indexPath, query] {
        QElapsedTimer timer;
        timer.start();
        Outcome outcome{Search::run(index, query), 0};
        outcome.elapsedMs = timer.elapsed();
        return outcome;
    }));
}

void SearchPanel::present(const std::expected<QList<Search::Hit>, QString>& result, qint64 elapsedMs)
{
    m_results->clear();
    if (!result) {
        m_status->setText(result.error());
        emit finished(0);
        return;
    }
    for (const Search::Hit& hit : *result) {
        auto* item = new QTreeWidgetItem(m_results, {hit.noteDisplay});
        item->setData(0, kPathRole, hit.notePath);
        item->setToolTip(0, hit.notePath);
        QFont bold = item->font(0);
        bold.setBold(true);
        item->setFont(0, bold);
        if (!hit.snippet.isEmpty()) {
            auto* line = new QTreeWidgetItem(item);
            auto* label = new QLabel(snippetHtml(hit.snippet));
            label->setWordWrap(true);
            label->setTextFormat(Qt::RichText);
            label->setForegroundRole(QPalette::PlaceholderText);
            m_results->setItemWidget(line, 0, label);
        }
        item->setExpanded(true);
    }
    if (m_input->text().trimmed().isEmpty())
        m_status->setText(tr("Type to search every note."));
    else
        m_status->setText(tr("%n result(s) in %1 ms", nullptr, int(result->size())).arg(elapsedMs));
    emit finished(int(result->size()));
}

} // namespace coeus
