#include "index/SearchQuery.hpp"

#include <QRegularExpression>

#include <algorithm>

namespace coeus {

namespace {

// Splits on whitespace, keeping "quoted runs" (and prefix:"quoted runs") whole.
// An unclosed quote runs to the end.
QStringList tokens(const QString& text)
{
    QStringList out;
    QString current;
    bool quoted = false;
    bool inRegex = false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (inRegex) {
            current += c;
            if (c == QLatin1Char('/') && current.size() > 4 && text[i - 1] != QLatin1Char('\\'))
                inRegex = false;
            continue;
        }
        if (c == QLatin1Char('"')) {
            quoted = !quoted;
            current += c;
        } else if (c.isSpace() && !quoted) {
            if (!current.isEmpty())
                out.append(current);
            current.clear();
        } else {
            current += c;
            if (current == QLatin1String("re:/") || current == QLatin1String("-re:/"))
                inRegex = true; // a regex may contain spaces and quotes
        }
    }
    if (!current.isEmpty())
        out.append(current);
    return out;
}

QString unquote(QString value)
{
    if (value.startsWith(QLatin1Char('"')))
        value.remove(0, 1);
    if (value.endsWith(QLatin1Char('"')))
        value.chop(1);
    return value;
}

} // namespace

bool SearchQuery::isEmpty() const
{
    return terms.isEmpty() && phrases.isEmpty() && excluded.isEmpty() && tags.isEmpty() && paths.isEmpty()
           && files.isEmpty() && patterns.isEmpty();
}

QStringList SearchQuery::highlightWords() const
{
    return phrases + terms + files;
}

QString SearchQuery::ftsString(const QString& text)
{
    QString escaped = text;
    escaped.replace(QLatin1Char('"'), QLatin1String("\"\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

QString SearchQuery::ftsExpression() const
{
    QStringList parts;
    for (const QString& phrase : phrases)
        parts.append(ftsString(phrase));
    for (const QString& term : terms)
        parts.append(ftsString(term) + QLatin1Char('*')); // prefix: results while typing
    return parts.join(QStringLiteral(" AND "));
}

SearchQuery SearchQuery::parse(const QString& text)
{
    SearchQuery query;
    for (const QString& token : tokens(text)) {
        const bool negated = token.startsWith(QLatin1Char('-')) && token.size() > 1;
        const QString body = negated ? token.mid(1) : token;

        auto valueAfter = [&body](const char* prefix) { return unquote(body.mid(qsizetype(qstrlen(prefix)))); };
        const bool isOperator = body.startsWith(QLatin1String("re:/")) || body.startsWith(QLatin1String("tag:"))
                                || body.startsWith(QLatin1String("path:")) || body.startsWith(QLatin1String("file:"));
        if (negated && isOperator) {
            query.error = QStringLiteral("%1: only words and phrases can be excluded with '-'").arg(token);
            continue;
        }
        if (body.startsWith(QLatin1String("re:/"))) {
            QString pattern = body.mid(4);
            if (pattern.endsWith(QLatin1Char('/')))
                pattern.chop(1);
            if (pattern.isEmpty())
                continue;
            const QRegularExpression check(pattern);
            if (!check.isValid())
                query.error = QStringLiteral("re:/%1/ — %2").arg(pattern, check.errorString());
            else
                query.patterns.append(pattern);
        } else if (body.startsWith(QLatin1String("tag:"))) {
            QString tag = valueAfter("tag:");
            if (tag.startsWith(QLatin1Char('#')))
                tag.remove(0, 1);
            if (!tag.isEmpty())
                query.tags.append(tag);
        } else if (body.startsWith(QLatin1String("path:"))) {
            if (const QString path = valueAfter("path:"); !path.isEmpty())
                query.paths.append(path);
        } else if (body.startsWith(QLatin1String("file:"))) {
            if (const QString file = valueAfter("file:"); !file.isEmpty())
                query.files.append(file);
        } else if (negated) {
            if (const QString word = unquote(body); !word.trimmed().isEmpty())
                query.excluded.append(word);
        } else if (body.startsWith(QLatin1Char('"'))) {
            if (const QString phrase = unquote(body); !phrase.trimmed().isEmpty())
                query.phrases.append(phrase);
        } else if (std::any_of(body.begin(), body.end(), [](QChar c) { return c.isLetterOrNumber(); })) {
            query.terms.append(body);
        }
        // A word of punctuation alone (a "-" about to become "-draft") finds nothing: skip it.
    }
    return query;
}

} // namespace coeus
