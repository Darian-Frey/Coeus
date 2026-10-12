#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace coeus {

// Structures the parser extracts from one note (SPEC.md §Markdown dialect).
// Line numbers are 1-based and count from the first line of the file,
// frontmatter included, so they match what an editor shows.

enum class LinkStyle {
    Wiki,     // [[target]], ![[target]]: title or root-relative path
    Markdown, // [text](path), ![alt](path): relative to the note's folder
};

struct Link {
    LinkStyle style = LinkStyle::Wiki;
    QString target;  // without heading or block suffix; never empty
    QString heading; // from target#Heading
    QString block;   // from target#^block or target^block
    QString alias;   // [[target|alias]] or the Markdown link text
    int line = 0;
    bool embed = false;
};

struct Tag {
    QString name; // without '#'; nested tags keep '/', e.g. "project/coeus"
    int line = 0;
};

struct Heading {
    int level = 1;
    QString text;
    int line = 0;
};

struct BlockId {
    QString id; // without '^'
    int line = 0;
};

enum class PropertyType { Text, Number, Date, List };

struct Property {
    QString key;
    QStringList values; // one entry unless type is List
    PropertyType type = PropertyType::Text;
    int line = 0;
};

struct FrontmatterError {
    QString message;
    int line = 0;
};

struct ParsedNote {
    QList<Property> properties;
    QStringList aliases; // from frontmatter aliases: (or alias:)
    QList<Link> links;
    QList<Tag> tags; // inline and frontmatter tags:, in document order
    QList<Heading> headings;
    QList<BlockId> blocks;
    QString plainText; // body text for full-text search, without frontmatter
    std::optional<FrontmatterError> frontmatterError;
    int bodyStartLine = 1; // first line after the frontmatter
};

} // namespace coeus
