#pragma once

#include <QStringView>

#include <optional>

namespace coeus {

// Case-insensitive subsequence match for the command palette.
// Returns std::nullopt when `pattern` is not a subsequence of `text`;
// otherwise a score where higher is better. An empty pattern scores 0.
// Consecutive matches and matches at word starts score more.
std::optional<int> fuzzyScore(QStringView pattern, QStringView text);

} // namespace coeus
