#include "ui/Shortcuts.hpp"

#include <QAction>
#include <QSettings>

namespace coeus {

namespace {

constexpr const char* kDefaultProperty = "coeusDefaultShortcut";

QString settingsKey(const QAction* action)
{
    QString id = Shortcuts::idOf(action);
    id.replace(QLatin1Char('/'), QLatin1Char('_')); // a slash would start a settings group
    return QStringLiteral("shortcuts/") + id;
}

} // namespace

QString Shortcuts::idOf(const QAction* action)
{
    if (!action->objectName().isEmpty())
        return action->objectName();
    QString text = action->text();
    text.remove(QLatin1Char('&'));
    if (text.endsWith(QStringLiteral("…")))
        text.chop(1);
    return text.trimmed();
}

void Shortcuts::adopt(QAction* action)
{
    if (!action->property(kDefaultProperty).isValid())
        action->setProperty(kDefaultProperty, QVariant::fromValue(action->shortcut()));
    const QSettings settings;
    const QString key = settingsKey(action);
    if (settings.contains(key))
        action->setShortcut(QKeySequence::fromString(settings.value(key).toString(), QKeySequence::PortableText));
}

QKeySequence Shortcuts::defaultOf(const QAction* action)
{
    const QVariant value = action->property(kDefaultProperty);
    return value.isValid() ? value.value<QKeySequence>() : action->shortcut();
}

QAction* Shortcuts::conflict(const QList<QAction*>& actions, const QAction* action, const QKeySequence& sequence)
{
    if (sequence.isEmpty())
        return nullptr;
    for (QAction* other : actions) {
        if (other != action && other->shortcut() == sequence)
            return other;
    }
    return nullptr;
}

void Shortcuts::set(QAction* action, const QKeySequence& sequence)
{
    QSettings settings;
    if (sequence == defaultOf(action))
        settings.remove(settingsKey(action));
    else
        settings.setValue(settingsKey(action), sequence.toString(QKeySequence::PortableText));
    action->setShortcut(sequence);
}

void Shortcuts::reset(QAction* action)
{
    set(action, defaultOf(action));
}

} // namespace coeus
