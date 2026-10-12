// Helper for test_atomic_writer: rewrites a file through AtomicWriter in a
// loop, alternating two contents, until it is killed.
//
//   atomic_write_helper <path> <bytes>

#include "vault/AtomicWriter.hpp"

#include <QByteArray>
#include <QString>

#include <cstdio>
#include <cstdlib>

int main(int argc, char* argv[])
{
    if (argc != 3) {
        std::fprintf(stderr, "usage: atomic_write_helper <path> <bytes>\n");
        return 2;
    }
    const QString path = QString::fromLocal8Bit(argv[1]);
    const qsizetype bytes = std::atoll(argv[2]);
    const QByteArray contents[2] = {QByteArray(bytes, 'a'), QByteArray(bytes, 'b')};

    std::puts("ready");
    std::fflush(stdout);
    for (unsigned i = 0;; ++i) {
        if (const auto result = coeus::AtomicWriter::write(path, contents[i % 2]); !result) {
            std::fprintf(stderr, "%s\n", qPrintable(result.error()));
            return 1;
        }
    }
}
