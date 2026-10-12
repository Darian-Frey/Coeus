#pragma once

#include <QByteArray>
#include <QByteArrayView>

namespace coeus {

// BLAKE2b-128 of a note's or chunk's bytes (SPEC.md §Index schema, D-017).
// Fixed by SPEC: changing it invalidates every hash column and the sidecar.
class ContentHash {
public:
    static constexpr int kBytes = 16;
    static QByteArray of(QByteArrayView data);
};

} // namespace coeus
