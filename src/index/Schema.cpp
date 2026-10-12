#include "index/Schema.hpp"

namespace coeus {

namespace {

// Phase 2 tables. Registers, ideas, chunks and vectors arrive with Phase 3
// and a version bump.
constexpr const char* kSchemaSql = R"sql(
CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT);

CREATE TABLE roots (
  id INTEGER PRIMARY KEY,
  name TEXT UNIQUE NOT NULL,
  path TEXT NOT NULL,
  present INTEGER NOT NULL DEFAULT 1
);

CREATE TABLE projects (
  id INTEGER PRIMARY KEY,
  root_id INTEGER NOT NULL REFERENCES roots(id),
  path TEXT NOT NULL,
  status TEXT NOT NULL,
  provenance TEXT, last_reviewed TEXT, why TEXT,
  UNIQUE(root_id, path)
);

CREATE TABLE notes (
  id INTEGER PRIMARY KEY,
  root_id INTEGER NOT NULL REFERENCES roots(id),
  path TEXT NOT NULL,
  title TEXT NOT NULL,
  mtime INTEGER NOT NULL,
  size INTEGER NOT NULL,
  hash BLOB NOT NULL,
  project_id INTEGER REFERENCES projects(id),
  UNIQUE(root_id, path)
);
CREATE INDEX notes_title ON notes(title);

CREATE TABLE links (
  src INTEGER NOT NULL REFERENCES notes(id) ON DELETE CASCADE,
  target_note INTEGER REFERENCES notes(id) ON DELETE SET NULL,
  target_raw TEXT NOT NULL,
  style TEXT NOT NULL DEFAULT 'wiki',
  heading TEXT, block TEXT, alias TEXT,
  line INTEGER NOT NULL,
  embed INTEGER NOT NULL DEFAULT 0,
  ambiguous INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX links_target ON links(target_note);
CREATE INDEX links_src ON links(src);

CREATE TABLE tags (
  note INTEGER NOT NULL REFERENCES notes(id) ON DELETE CASCADE,
  tag TEXT NOT NULL,
  line INTEGER
);
CREATE INDEX tags_tag ON tags(tag);
CREATE INDEX tags_note ON tags(note);

CREATE TABLE props (
  note INTEGER NOT NULL REFERENCES notes(id) ON DELETE CASCADE,
  key TEXT NOT NULL, value TEXT, type TEXT NOT NULL,
  line INTEGER
);
CREATE INDEX props_note ON props(note);
CREATE INDEX props_key ON props(key);

CREATE TABLE headings (
  note INTEGER NOT NULL REFERENCES notes(id) ON DELETE CASCADE,
  level INTEGER NOT NULL, text TEXT NOT NULL, line INTEGER NOT NULL
);
CREATE INDEX headings_note ON headings(note);

CREATE TABLE blocks (
  note INTEGER NOT NULL REFERENCES notes(id) ON DELETE CASCADE,
  block_id TEXT NOT NULL, line INTEGER NOT NULL
);
CREATE INDEX blocks_note ON blocks(note);

CREATE VIRTUAL TABLE fts USING fts5(
  title, body, path UNINDEXED, tokenize='unicode61 remove_diacritics 2'
);
)sql";

} // namespace

std::expected<int, QString> Schema::version(Database& db)
{
    auto exists = db.prepare("SELECT 1 FROM sqlite_master WHERE type='table' AND name='meta'");
    if (!exists)
        return std::unexpected(exists.error());
    const auto hasMeta = exists->step();
    if (!hasMeta)
        return std::unexpected(hasMeta.error());
    if (!*hasMeta)
        return 0;

    auto query = db.prepare("SELECT value FROM meta WHERE key='schema_version'");
    if (!query)
        return std::unexpected(query.error());
    const auto row = query->step();
    if (!row)
        return std::unexpected(row.error());
    return *row ? int(query->text(0).toInt()) : 0;
}

std::expected<void, QString> Schema::create(Database& db)
{
    auto transaction = Transaction::begin(db, Transaction::Kind::Immediate);
    if (!transaction)
        return std::unexpected(transaction.error());
    if (auto result = db.exec(kSchemaSql); !result)
        return std::unexpected(result.error());
    auto insert = db.prepare("INSERT INTO meta(key, value) VALUES ('schema_version', ?)");
    if (!insert)
        return std::unexpected(insert.error());
    if (auto result = insert->bind(1, QString::number(kVersion)).run(); !result)
        return std::unexpected(result.error());
    return transaction->commit();
}

} // namespace coeus
