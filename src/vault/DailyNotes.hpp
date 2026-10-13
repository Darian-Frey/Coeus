#pragma once

#include "vault/Vault.hpp"

#include <QDate>
#include <QString>

#include <expected>

namespace coeus {

// Daily notes (F-009): one note per day at a path built from the date.
// Settings come from config.toml [daily]:
//   folder   = "daily"        relative to the vault
//   format   = "yyyy-MM-dd"   a Qt date format; '/' makes subfolders
//   template = "templates/Daily.md"   optional, relative to the vault
class DailyNotes {
public:
    struct Settings {
        QString folder = QStringLiteral("daily");
        QString format = QStringLiteral("yyyy-MM-dd");
        QString templatePath; // empty for none
    };

    static Settings settings(const QString& vaultPath);

    // The note for `date`, absolute. Fails if the settings would put it
    // outside the vault or give it an empty name.
    static std::expected<QString, QString> pathFor(const QString& vaultPath, const QDate& date, const Settings& settings);

    struct Opened {
        QString path;
        bool created = false;
        QStringList unsupported; // template variables left as written
    };

    // Today's note: created (from the template, if any) when it does not
    // exist, through Vault::writeNote and AtomicWriter; never overwritten.
    // An explicit user command (CLAUDE.md §Invariants).
    static std::expected<Opened, QString> openOrCreate(const Vault& vault, const QDateTime& now);
};

} // namespace coeus
