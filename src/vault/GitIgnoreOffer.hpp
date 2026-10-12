#pragma once

#include <QString>
#include <QStringList>

#include <expected>

namespace coeus {

// The first-open offer to keep Coeus's own files out of the vault's Git
// repository (SPEC.md §Vault layout, BUG-003). Coeus never edits .gitignore
// unasked: accept() is an explicit user command and goes through AtomicWriter.
class GitIgnoreOffer {
public:
    static constexpr const char* kIndexPattern = ".coeus/";
    static constexpr const char* kTempPattern = ".*.coeus-tmp-*";
    static constexpr const char* kComment = "# Coeus: machine-local index and files left by interrupted saves";

    // Patterns the vault's Git rules do not yet cover. Empty when the vault is
    // not in a repository or everything is already ignored. Opens a
    // repository: call it off the UI thread.
    static QStringList missingPatterns(const QString& vaultPath);

    // Whether the author chose "Don't Ask Again" for this vault.
    static bool declined(const QString& vaultPath);

    // Appends `patterns` under kComment to <vault>/.gitignore, creating it if needed.
    static std::expected<void, QString> accept(const QString& vaultPath, const QStringList& patterns);
    // Records [vault] gitignore_offer = "declined" in config.toml.
    static std::expected<void, QString> decline(const QString& vaultPath);
};

} // namespace coeus
