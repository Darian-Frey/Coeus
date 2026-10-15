#pragma once

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QStringList>

namespace coeus {

// Note templates (F-010) and the text substitutions they share with daily
// notes (F-009). Rendering is pure; listing reads the templates folder.
class Templates {
public:
    static constexpr const char* kDefaultFolder = "templates";
    // A template's frontmatter key naming the folder, relative to the vault,
    // that File › New Note… puts its notes in (F-036). Never copied into a note.
    static constexpr const char* kFolderKey = "coeus-folder";

    struct Rendered {
        QString text;
        QStringList unsupported; // variables left as written, e.g. {{next:F}} outside a project
    };

    // Replaces {{date}} (yyyy-MM-dd), {{time}} (HH:mm), {{title}}, and
    // {{date:FORMAT}} / {{time:FORMAT}} with Qt date-time formats, and
    // {{next:KIND}} with the ID allocated for KIND in `nextIds` (one ID per
    // kind per insertion). Unknown variables, and {{next:KIND}} with no ID,
    // are left as written and listed in `unsupported`. kFolderKey is removed.
    static Rendered render(const QString& text, const QDateTime& now, const QString& title,
                           const QHash<QString, QString>& nextIds = {});
    // The folder `text`'s kFolderKey names, cleaned; empty if none.
    static QString placement(const QString& text);
    // `text` without its kFolderKey; frontmatter left empty goes too.
    static QString withoutPlacement(const QString& text);
    // The register kinds a template asks {{next:KIND}} for, each once.
    static QStringList nextKinds(const QString& text);

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
