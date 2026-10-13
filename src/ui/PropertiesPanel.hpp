#pragma once

#include "parser/ParsedNote.hpp"

#include <QWidget>

class QGridLayout;
class QLabel;
class QLineEdit;
class QTimer;
class QToolButton;

namespace coeus {

class Editor;

// The note's frontmatter as typed fields, above the source editor (F-008).
// Edits go into the editor's buffer, one key's lines at a time (D-015), so
// they are undoable and saved like any other change. A value outside its
// vocabulary, or a field that does not hold its type, is shown as an error
// and never rewritten.
class PropertiesPanel : public QWidget {
    Q_OBJECT

public:
    explicit PropertiesPanel(Editor* editor, QWidget* parent = nullptr);

    // Re-reads the frontmatter from the buffer now.
    void refresh();

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
