#pragma once

#include <QString>

#include <expected>
#include <optional>

namespace coeus {

// Single settings in <vault>/.coeus/config.toml (D-016). Coeus edits the file
// line by line so the author's comments, order and formatting survive, and
// never writes a file it cannot parse.
class VaultConfig {
public:
    // The string at [table] key, if present and a string.
    static std::optional<QString> string(const QString& vaultPath, const char* table, const char* key);

    // Sets [table] key = "value": replaces the key's line, or adds it under
    // the table's header, or appends the table. Written through AtomicWriter.
    static std::expected<void, QString> setString(const QString& vaultPath, const char* table, const char* key,
                                                  const QString& value);

    // The same edit on text, for tests; fails if the text or result is not valid TOML.
    static std::expected<QByteArray, QString> withString(const QByteArray& toml, const char* table, const char* key,
                                                         const QString& value);
};

} // namespace coeus
