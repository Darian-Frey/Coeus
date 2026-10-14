#include "registers/ProjectScaffold.hpp"

#include "parser/Frontmatter.hpp"
#include "parser/RegisterParser.hpp"
#include "registers/RegisterTemplates.hpp"

#include <QRegularExpression>
#include <QStringList>

namespace coeus {

std::optional<QString> ProjectScaffold::withLastReviewed(const QString& readme, const QDate& today)
{
    if (!RegisterParser::statusHeader(readme))
        return std::nullopt;
    // The third line is Last reviewed (the header's order is fixed); keep its
    // prefix and any line ending exactly as written.
    static const QRegularExpression kLine(QStringLiteral(R"(^(> \*\*Last reviewed:\*\* )[^\r\n]*)"),
                                          QRegularExpression::MultilineOption);
    qsizetype start = 0;
    for (int line = 0; line < 2; ++line) {
        start = readme.indexOf(QLatin1Char('\n'), start);
        if (start < 0)
            return std::nullopt;
        ++start;
    }
    const auto m = kLine.match(readme, start, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
    if (!m.hasMatch())
        return std::nullopt;
    QString result = readme;
    result.replace(m.capturedStart(), m.capturedLength(), m.captured(1) + today.toString(Qt::ISODate));
    return result;
}

QString ProjectScaffold::folderName(const QString& codename)
{
    static const QRegularExpression kSafe(QStringLiteral(R"(^[\p{L}\p{N}][\p{L}\p{N} ._-]*$)"));
    const QString name = codename.trimmed();
    return kSafe.match(name).hasMatch() ? name : QString();
}

QString ProjectScaffold::rootName(const QString& codename)
{
    QString name = codename.trimmed().toLower();
    name.replace(QLatin1Char(' '), QLatin1Char('-'));
    name.remove(QRegularExpression(QStringLiteral(R"([^a-z0-9._-])")));
    return name == QLatin1String("vault") ? QStringLiteral("vault-project") : name;
}

QList<QPair<QString, QString>> ProjectScaffold::promotion(const QString& ideaText, const QString& ideaPath,
                                                          const QString& codename, const QDate& today)
{
    const QString date = today.toString(Qt::ISODate);
    const QByteArray bytes = ideaText.toUtf8();
    const Frontmatter::Split split = Frontmatter::split(bytes);
    ParsedNote idea;
    if (split.present)
        Frontmatter::parse(split, idea);
    const auto value = [&idea](const char* key) {
        for (const Property& property : std::as_const(idea.properties)) {
            if (property.key == QLatin1String(key))
                return property.values.join(QStringLiteral(", ")).trimmed();
        }
        return QString();
    };
    QString body = split.present ? QString::fromUtf8(split.body) : ideaText;
    while (body.startsWith(QLatin1Char('\n')))
        body.remove(0, 1);

    QString readme = QStringLiteral("> **Status:** Active\n> **Provenance:** Promoted from the idea %1 on %2\n"
                                    "> **Last reviewed:** %2\n> **Why this status:** Newly promoted from an idea.\n\n")
                         .arg(ideaPath, date);
    if (!body.trimmed().startsWith(QLatin1String("# ")))
        readme += QStringLiteral("# %1\n\n").arg(codename);
    else {
        // Keep the idea's own title as the README's heading.
        const qsizetype end = body.indexOf(QLatin1Char('\n'));
        readme += (end < 0 ? body : body.left(end)) + QStringLiteral("\n\n");
        body = end < 0 ? QString() : body.mid(end + 1);
        while (body.startsWith(QLatin1Char('\n')))
            body.remove(0, 1);
    }
    if (const QString summary = value("summary"); !summary.isEmpty())
        readme += summary + QStringLiteral("\n\n");
    if (const QString repo = value("repo"); !repo.isEmpty())
        readme += QStringLiteral("Repository: %1\n\n").arg(repo);
    readme += body;
    if (!readme.endsWith(QLatin1Char('\n')))
        readme += QLatin1Char('\n');

    const QString roadmap = QStringLiteral(
        "# Roadmap\n\n## Phase 0 — Scaffolding\n**Goal:** Documents to the standard and the decisions that shape the first build.\n"
        "**Status:** Not started\n**Deliverables:**\n- [ ] FEATURES, DECISIONS and this roadmap filled in\n"
        "**Acceptance:** The first phase that builds something is defined.\n");
    const QString claude = QStringLiteral(
        "# CLAUDE.md\n\n## Project\n%1: one paragraph on what it is and why.\n\n## Current state\nPromoted from an idea on %2; "
        "nothing is built yet.\n\n## Active task\nPhase 0 (ROADMAP.md).\n\n## Invariants\n\n## Build & test\n\n## Conventions\n\n"
        "## Out of scope\n")
                               .arg(codename, date);
    const QString changelog = QStringLiteral("# Changelog\n\nFormat follows [Keep a Changelog](https://keepachangelog.com).\n\n"
                                             "## [Unreleased]\n### Added\n- Project promoted from an idea (%1).\n")
                                  .arg(date);
    return {
        {QStringLiteral("README.md"), readme},
        {QStringLiteral("FEATURES.md"), RegisterTemplates::fileSkeleton(QStringLiteral("F"))},
        {QStringLiteral("ROADMAP.md"), roadmap},
        {QStringLiteral("DECISIONS.md"), RegisterTemplates::fileSkeleton(QStringLiteral("D"))},
        {QStringLiteral("CLAUDE.md"), claude},
        {QStringLiteral("CHANGELOG.md"), changelog},
    };
}

} // namespace coeus
