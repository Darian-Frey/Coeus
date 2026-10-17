#pragma once

#include "parser/ParsedNote.hpp"
#include "vault/PropertyVocabulary.hpp"

#include <QHash>
#include <QSet>
#include <QWidget>

#include <optional>

class QGridLayout;
class QLabel;
class QLineEdit;
class QTimer;
class QToolButton;

namespace coeus {

class Editor;

// The note's frontmatter as typed fields, above the source editor (F-008).
// Edits go into the editor's buffer, one key's lines at a time (D-015), so
// they are undoable and saved like any other change. A key with values
// declared in the vault's `Coeus properties.md` (or Coeus's own, D-025) is
// a drop-down; a value outside them, or a field that does not hold its
// type, is shown as an error and never rewritten. Other text fields suggest
// the values the vault already gives the key, read from the index off the
// UI thread.
class PropertiesPanel : public QWidget {
    Q_OBJECT

public:
    explicit PropertiesPanel(Editor* editor, QWidget* parent = nullptr);

    // Re-reads the frontmatter from the buffer now.
    void refresh();
    // Where the note is: for the declared values and the suggestions.
    void setContext(const QString& vaultPath, const QString& indexPath, const QString& displayPath, bool inVault);

    const QList<Property>& properties() const { return m_properties; }
    int errorCount() const { return m_errors; }
    // The field editing `key` (QLineEdit or QComboBox), for tests.
    QWidget* field(const QString& key) const;
    QToolButton* addButton() const { return m_add; }
    QLineEdit* newKey() const { return m_newKey; }

    static constexpr int kRefreshDelayMs = 300;

signals:
    void refreshed(int properties, int errors);

private:
    void rebuild();
    void commit(const QString& key, const QString& yamlValue);
    void remove(const QString& key);
    void addKey();
    QString check(const Property& property) const; // error text, or empty
    std::optional<PropertyVocabulary::Allowed> allowedFor(const QString& key) const;
    void suggest(const QString& key);

    QString m_vaultPath;
    QString m_indexPath;
    QString m_displayPath;
    bool m_inVault = true;
    QHash<QString, QStringList> m_suggestions; // by key, once read
    QSet<QString> m_asked;

    Editor* m_editor;
    QList<Property> m_properties;
    QString m_yamlError;
    int m_errors = 0;
    QToolButton* m_header = nullptr;
    QLabel* m_status = nullptr;
    QToolButton* m_add = nullptr;
    QLineEdit* m_newKey = nullptr;
    QWidget* m_body = nullptr;
    QGridLayout* m_grid = nullptr;
    QTimer* m_refreshTimer = nullptr;
};

} // namespace coeus
