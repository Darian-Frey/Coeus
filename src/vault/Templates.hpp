#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

namespace coeus {

// Note templates (F-010) and the text substitutions they share with daily
// notes (F-009). Rendering is pure; listing reads the templates folder.
class Templates {
public:
    static constexpr const char* kDefaultFolder = "templates";

    struct Rendered {
        QString text;
        QStringList unsupported; // variables left as written, e.g. {{next:F}} until Phase 3
    };

    // Replaces {{date}} (yyyy-MM-dd), {{time}} (HH:mm), {{title}}, and
    // {{date:FORMAT}} / {{time:FORMAT}} with Qt date-time formats. Unknown
    // variables are left as written and listed in `unsupported`.
    static Rendered render(const QString& text, const QDateTime& now, const QString& title);

    // The templates folder from config.toml [templates] folder, relative to the vault.
    static QString folder(const QString& vaultPath);
    // Every .md file under the templates folder, as absolute paths sorted by name.
    static QStringList list(const QString& vaultPath);

    // The text that inserting `rendered` into `note` at `cursor` produces:
    // the template's body goes in at the cursor, and its frontmatter keys are
    // added to the note's frontmatter where the note lacks them, existing keys
    // untouched (D-015). An empty note simply receives the whole template.
    static QString insert(const QString& note, int cursor, const QString& rendered, int* newCursor = nullptr);
};

} // namespace coeus
