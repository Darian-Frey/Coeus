#include "ui/PropertiesPanel.hpp"

#include "parser/Frontmatter.hpp"
#include "registers/Vocabulary.hpp"
#include "ui/Editor.hpp"

#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace coeus {

namespace {

const QRegularExpression& numberPattern()
{
    static const QRegularExpression kNumber(QStringLiteral(R"(^[-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?$)"));
    return kNumber;
}

bool isIsoDate(const QString& value)
{
    static const QRegularExpression kDate(QStringLiteral(R"(^\d{4}-\d{2}-\d{2}$)"));
    return kDate.match(value).hasMatch() && QDate::fromString(value, Qt::ISODate).isValid();
}

QString typeName(PropertyType type, bool isEnum)
{
    if (isEnum)
        return QStringLiteral("enum");
    switch (type) {
    case PropertyType::Number:
        return QStringLiteral("number");
    case PropertyType::Date:
        return QStringLiteral("date");
    case PropertyType::List:
        return QStringLiteral("list");
    case PropertyType::Text:
        break;
    }
    return QStringLiteral("text");
}

QLabel* errorLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setStyleSheet(QStringLiteral("color: #c0392b;"));
    return label;
}

} // namespace

PropertiesPanel::PropertiesPanel(Editor* editor, QWidget* parent)
    : QWidget(parent)
    , m_editor(editor)
    , m_header(new QToolButton(this))
    , m_status(new QLabel(this))
    , m_add(new QToolButton(this))
    , m_newKey(new QLineEdit(this))
    , m_body(new QWidget(this))
    , m_grid(new QGridLayout(m_body))
    , m_refreshTimer(new QTimer(this))
{
    m_header->setCheckable(true);
    m_header->setChecked(true);
    m_header->setAutoRaise(true);
    m_header->setArrowType(Qt::DownArrow);
    m_header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(m_header, &QToolButton::toggled, this, [this](bool open) {
        m_body->setVisible(open);
        m_header->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    });
    m_status->setStyleSheet(QStringLiteral("color: #c0392b;"));
    m_add->setText(tr("+ Add"));
    m_add->setAutoRaise(true);
    m_newKey->setPlaceholderText(tr("property name, then Enter"));
    m_newKey->hide();
    connect(m_add, &QToolButton::clicked, this, [this] {
        m_header->setChecked(true);
        m_newKey->show();
        m_newKey->setFocus();
    });
    connect(m_newKey, &QLineEdit::returnPressed, this, &PropertiesPanel::addKey);

    auto* top = new QHBoxLayout;
    top->setContentsMargins(0, 0, 0, 0);
    top->addWidget(m_header);
    top->addWidget(m_status, 1);
    top->addWidget(m_add);

    m_grid->setContentsMargins(12, 0, 8, 6);
    m_grid->setHorizontalSpacing(10);
    m_grid->setVerticalSpacing(3);
    m_grid->setColumnStretch(2, 1);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 0, 4, 4);
    layout->setSpacing(0);
    layout->addLayout(top);
    layout->addWidget(m_body);
    layout->addWidget(m_newKey);

    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(kRefreshDelayMs);
    connect(m_refreshTimer, &QTimer::timeout, this, &PropertiesPanel::refresh);
    connect(m_editor, &QPlainTextEdit::textChanged, m_refreshTimer, qOverload<>(&QTimer::start));
    refresh();
}

void PropertiesPanel::refresh()
{
    m_refreshTimer->stop();
    const QByteArray text = m_editor->toPlainText().toUtf8();
    ParsedNote note;
    Frontmatter::parse(Frontmatter::split(text), note);
    m_properties = note.properties;
    m_yamlError = note.frontmatterError ? note.frontmatterError->message : QString();
    rebuild();
}

QString PropertiesPanel::check(const Property& property) const
{
    if (const auto allowed = Vocabulary::forProperty(property.key)) {
        const QString value = property.values.value(0);
        if (property.type == PropertyType::List || property.values.size() != 1 || !allowed->contains(value))
            return tr("“%1” is not one of: %2").arg(property.values.join(QStringLiteral(", ")),
                                                      allowed->join(QStringLiteral(", ")));
    }
    return {};
}

QWidget* PropertiesPanel::field(const QString& key) const
{
    return m_body->findChild<QWidget*>(QStringLiteral("field:") + key);
}

void PropertiesPanel::rebuild()
{
    // Clear the grid.
    // deleteLater: a field may be the sender of the signal that led here.
    while (QLayoutItem* item = m_grid->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->hide();
            widget->setObjectName(QString()); // not found by field() while it waits
            widget->deleteLater();
        }
        delete item;
    }
    m_errors = 0;
    const bool readOnly = !m_yamlError.isEmpty();
    int row = 0;
    if (readOnly) {
        m_grid->addWidget(errorLabel(tr("Frontmatter is not valid YAML, so it cannot be edited here: %1").arg(m_yamlError), m_body),
                          row++, 0, 1, 5);
        m_errors = 1;
    }
    for (const Property& property : std::as_const(m_properties)) {
        const auto allowed = Vocabulary::forProperty(property.key);
        auto* key = new QLabel(property.key, m_body);
        key->setForegroundRole(QPalette::PlaceholderText);
        auto* type = new QLabel(typeName(property.type, allowed.has_value()), m_body);
        type->setForegroundRole(QPalette::PlaceholderText);
        QFont small = type->font();
        small.setPointSizeF(small.pointSizeF() * 0.85);
        type->setFont(small);

        QWidget* editor = nullptr;
        const QString problem = check(property);
        if (allowed) {
            auto* combo = new QComboBox(m_body);
            const QString current = property.values.join(QStringLiteral(", "));
            if (!problem.isEmpty())
                combo->addItem(current); // shown as written, never replaced unasked
            combo->addItems(*allowed);
            combo->setCurrentText(current);
            const QString keyName = property.key;
            connect(combo, &QComboBox::activated, this, [this, combo, keyName, allowed](int) {
                if (allowed->contains(combo->currentText()))
                    commit(keyName, Frontmatter::yamlScalar(combo->currentText()));
            });
            editor = combo;
        } else {
            auto* line = new QLineEdit(property.values.join(QStringLiteral(", ")), m_body);
            const Property original = property;
            connect(line, &QLineEdit::editingFinished, this, [this, line, original] {
                const QString text = line->text().trimmed();
                if (text == original.values.join(QStringLiteral(", ")))
                    return; // unchanged
                switch (original.type) {
                case PropertyType::List: {
                    QStringList items;
                    for (const QString& item : text.split(QLatin1Char(','), Qt::SkipEmptyParts))
                        items.append(item.trimmed());
                    commit(original.key, Frontmatter::yamlList(items));
                    break;
                }
                case PropertyType::Number:
                    if (!numberPattern().match(text).hasMatch()) {
                        line->setStyleSheet(QStringLiteral("color: #c0392b;"));
                        line->setToolTip(tr("Not a number; nothing was changed."));
                        return;
                    }
                    commit(original.key, text);
                    break;
                case PropertyType::Date:
                    if (!isIsoDate(text)) {
                        line->setStyleSheet(QStringLiteral("color: #c0392b;"));
                        line->setToolTip(tr("Dates are YYYY-MM-DD; nothing was changed."));
                        return;
                    }
                    commit(original.key, text);
                    break;
                case PropertyType::Text:
                    commit(original.key, Frontmatter::yamlScalar(text));
                    break;
                }
            });
            editor = line;
        }
        editor->setObjectName(QStringLiteral("field:") + property.key);
        editor->setEnabled(!readOnly);

        auto* removeButton = new QToolButton(m_body);
        removeButton->setText(QStringLiteral("×"));
        removeButton->setAutoRaise(true);
        removeButton->setToolTip(tr("Remove %1").arg(property.key));
        removeButton->setObjectName(QStringLiteral("remove:") + property.key);
        removeButton->setEnabled(!readOnly);
        const QString keyName = property.key;
        connect(removeButton, &QToolButton::clicked, this, [this, keyName] { remove(keyName); });

        m_grid->addWidget(key, row, 0);
        m_grid->addWidget(type, row, 1);
        m_grid->addWidget(editor, row, 2);
        m_grid->addWidget(removeButton, row, 3);
        ++row;
        if (!problem.isEmpty()) {
            ++m_errors;
            m_grid->addWidget(errorLabel(problem, m_body), row++, 2, 1, 2);
        }
    }

    m_header->setText(tr("Properties (%1)").arg(m_properties.size()));
    m_status->setText(m_errors > 0 ? tr("%n problem(s)", nullptr, m_errors) : QString());
    m_add->setEnabled(!readOnly);
    m_body->setVisible(m_header->isChecked() && row > 0);
    emit refreshed(int(m_properties.size()), m_errors);
}

void PropertiesPanel::commit(const QString& key, const QString& yamlValue)
{
    const Frontmatter::Edit edit = Frontmatter::setValue(m_editor->toPlainText(), key, yamlValue);
    QTextCursor cursor(m_editor->document());
    cursor.beginEditBlock(); // one undo step
    cursor.setPosition(edit.start);
    cursor.setPosition(edit.start + edit.length, QTextCursor::KeepAnchor);
    cursor.insertText(edit.replacement);
    cursor.endEditBlock();
    // Queued: we are inside the signal of a field that refresh() replaces.
    QMetaObject::invokeMethod(this, &PropertiesPanel::refresh, Qt::QueuedConnection);
}

void PropertiesPanel::remove(const QString& key)
{
    const auto edit = Frontmatter::removeKey(m_editor->toPlainText(), key);
    if (!edit)
        return;
    QTextCursor cursor(m_editor->document());
    cursor.beginEditBlock();
    cursor.setPosition(edit->start);
    cursor.setPosition(edit->start + edit->length, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    cursor.endEditBlock();
    QMetaObject::invokeMethod(this, &PropertiesPanel::refresh, Qt::QueuedConnection);
}

void PropertiesPanel::addKey()
{
    const QString key = m_newKey->text().trimmed();
    m_newKey->clear();
    m_newKey->hide();
    if (key.isEmpty() || std::any_of(m_properties.begin(), m_properties.end(),
                                     [&key](const Property& p) { return p.key == key; }))
        return;
    const auto allowed = Vocabulary::forProperty(key);
    commit(key, allowed ? Frontmatter::yamlScalar(allowed->front()) : Frontmatter::yamlScalar(QString()));
    QMetaObject::invokeMethod(this, [this, key] {
        if (QWidget* added = field(key))
            added->setFocus();
    }, Qt::QueuedConnection);
}

} // namespace coeus
