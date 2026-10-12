#include "index/ContentHash.hpp"

#include <QtGlobal>

#include <sodium.h>

#include <mutex>

namespace coeus {

QByteArray ContentHash::of(QByteArrayView data)
{
    static std::once_flag initialised;
    std::call_once(initialised, [] {
        if (sodium_init() < 0)
            qFatal("libsodium failed to initialise");
    });

    QByteArray hash(kBytes, Qt::Uninitialized);
    crypto_generichash(reinterpret_cast<unsigned char*>(hash.data()), kBytes,
                       reinterpret_cast<const unsigned char*>(data.data()), static_cast<unsigned long long>(data.size()),
                       nullptr, 0);
    return hash;
}

} // namespace coeus
