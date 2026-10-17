#include "vault/PropertyVocabulary.hpp"

#include "registers/Vocabulary.hpp"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QRegularExpression>

namespace coeus {

namespace {

// `value` without the backticks a writer may put round it.
QString bare(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2 && value.startsWith(QLatin1Char('`')) && value.endsWith(QLatin1Char('`')))
        value = value.mid(1, value.size() - 2).trimmed();
    return value;
}

} // namespace

QString PropertyVocabulary::filePath(const QString& vaultPath)
{
    return vaultPath + QLatin1Char('/') + QLatin1String(kFileName);
}

QList<PropertyVocabulary::List> PropertyVocabulary::parse(const QString& text)
{
    static const QRegularExpression kFence(QStringLiteral(R"(^ {0,3}(`{3,}|~{3,}))"));
    static const QRegularExpression kHeading(QStringLiteral(R"(^ {0,3}(#{1,6})\s+(.*?)\s*#*\s*$)"));
    static const QRegularExpression kItem(QStringLiteral(R"(^ {0,3}[-*+]\s+(.*)$)"));
    static const QRegularExpression kFolders(QStringLiteral(R"(^ {0,3}Folders?:\s*(.*)$)"), QRegularExpression::CaseInsensitiveOption);

    QList<List> lists;
    std::optional<List> current;
    const auto finish = [&] {
        if (current && !current->key.isEmpty() && !current->values.isEmpty())
            lists.append(*current);
        current.reset();
    };
    QString fence;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString& line = lines[i];
        if (const auto open = kFence.match(line); open.hasMatch()) {
            const QString marker = open.captured(1);
            if (fence.isEmpty())
                fence = marker;
            else if (marker.front() == fence.front() && marker.size() >= fence.size())
                fence.clear();
            continue;
        }
        if (!fence.isEmpty())
            continue;
        if (const auto heading = kHeading.match(line); heading.hasMatch()) {
            finish();
            if (heading.captured(1).size() == 2)
                current = List{bare(heading.captured(2)), {}, {}, i + 1};
            continue;
        }
        if (!current)
            continue;
        if (const auto folders = kFolders.match(line); folders.hasMatch()) {
            for (const QString& folder : folders.captured(1).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                QString name = bare(folder);
                while (name.endsWith(QLatin1Char('/')))
                    name.chop(1);
                if (!name.isEmpty())
                    current->folders.append(name);
            }
        } else if (const auto item = kItem.match(line); item.hasMatch()) {
            const QString value = bare(item.captured(1));
            if (!value.isEmpty() && !current->values.contains(value))
                current->values.append(value);
        }
    }
    finish();
    return lists;
}

QList<PropertyVocabulary::List> PropertyVocabulary::load(const QString& vaultPath)
{
    struct Cached {
        qint64 size = -1;
        qint64 modified = -1;
        QList<List> lists;
    };
    static QMutex mutex;
    static QHash<QString, Cached> cache;
    const QFileInfo info(filePath(vaultPath));
    const qint64 size = info.exists() ? info.size() : -1;
    const qint64 modified = info.exists() ? info.lastModified().toMSecsSinceEpoch() : -1;
    QMutexLocker lock(&mutex);
    Cached& entry = cache[vaultPath];
    if (entry.size != size || entry.modified != modified) {
        entry = {size, modified, {}};
        QFile file(info.absoluteFilePath());
        if (file.open(QIODevice::ReadOnly))
            entry.lists = parse(QString::fromUtf8(file.readAll()));
    }
    return entry.lists;
}

std::optional<PropertyVocabulary::Allowed> PropertyVocabulary::allowed(const QList<List>& lists, const QString& key,
                                                                        const QString& displayPath, bool inVault)
{
    // The nearest folder above the note wins; a list for every note comes last.
    const List* best = nullptr;
    qsizetype bestDepth = -1;
    for (const List& list : lists) {
        if (list.key != key)
            continue;
        qsizetype depth = list.folders.isEmpty() ? 0 : -1;
        for (const QString& folder : list.folders)
            if (displayPath.startsWith(folder + QLatin1Char('/')))
                depth = std::max(depth, folder.size() + 1);
        if (depth > bestDepth) {
            best = &list;
            bestDepth = depth;
        }
    }
    if (best)
        return Allowed{best->values, true};
    if (key == QLatin1String("stage") && inVault && displayPath.startsWith(QLatin1String("ideas/")))
        return Allowed{Vocabulary::ideaStage(), false};
    return std::nullopt;
}

QByteArray PropertyVocabulary::starter()
{
    return "# Coeus properties\n"
           "\n"
           "The Properties card above each note offers the values listed here, in this order, and flags a value "
           "that is not one of them. Each level-2 heading is a property's name and the list under it its values. "
           "A `Folders:` line limits a list to the notes in those folders, named as the Explorer names them; "
           "the list for the nearest folder wins, and a list without one applies everywhere else. Anything else "
           "in this note is ignored, as is code, so write freely. A property listed nowhere suggests the values "
           "already used for it in the vault.\n"
           "\n"
           "For example, replacing the fence below with what it holds:\n"
           "\n"
           "```markdown\n"
           "## status\n"
           "Folders: prose\n"
           "- outline\n"
           "- draft\n"
           "- revised\n"
           "- final\n"
           "\n"
           "## pov\n"
           "- Theo Hartmann\n"
           "```\n";
}

} // namespace coeus
