#pragma once

#include "parser/ParsedNote.hpp"

#include <QByteArrayView>

namespace coeus {

// Parses one note into the structures the index stores (D-014).
// Pure: the same bytes always give the same result; no filesystem, clock or
// event loop (ARCHITECTURE.md invariant 6).
class NoteParser {
public:
    static ParsedNote parse(QByteArrayView utf8);
};

} // namespace coeus
