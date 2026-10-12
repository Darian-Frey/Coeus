#pragma once

#include <QByteArrayView>

#include <algorithm>
#include <vector>

namespace coeus {

// Maps byte offsets in a UTF-8 buffer to 1-based line numbers.
class LineIndex {
public:
    explicit LineIndex(QByteArrayView text)
    {
        m_starts.push_back(0);
        for (qsizetype i = 0; i < text.size(); ++i) {
            if (text[i] == '\n')
                m_starts.push_back(i + 1);
        }
    }

    int lineAt(qsizetype offset) const
    {
        const auto next = std::upper_bound(m_starts.begin(), m_starts.end(), offset);
        return int(next - m_starts.begin());
    }

    int lineCount() const { return int(m_starts.size()); }

private:
    std::vector<qsizetype> m_starts;
};

} // namespace coeus
