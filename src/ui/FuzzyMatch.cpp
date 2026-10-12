#include "ui/FuzzyMatch.hpp"

namespace coeus {

namespace {

constexpr int kMatchScore = 1;
constexpr int kConsecutiveBonus = 5;
constexpr int kWordStartBonus = 8;

bool isWordStart(QStringView text, qsizetype i)
{
    if (i == 0)
        return true;
    const QChar prev = text[i - 1];
    if (!prev.isLetterOrNumber())
        return true;
    return prev.isLower() && text[i].isUpper();
}

} // namespace

std::optional<int> fuzzyScore(QStringView pattern, QStringView text)
{
    int score = 0;
    qsizetype t = 0;
    qsizetype previous = -2;

    for (const QChar p : pattern) {
        const QChar wanted = p.toCaseFolded();
        while (t < text.size() && text[t].toCaseFolded() != wanted)
            ++t;
        if (t == text.size())
            return std::nullopt;

        score += kMatchScore;
        if (t == previous + 1)
            score += kConsecutiveBonus;
        if (isWordStart(text, t))
            score += kWordStartBonus;

        previous = t;
        ++t;
    }
    return score;
}

} // namespace coeus
