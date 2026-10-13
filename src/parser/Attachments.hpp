#pragma once

#include <QFileInfo>
#include <QSet>
#include <QString>

namespace coeus {

// Targets that name an attachment (image, PDF, audio, video, canvas) rather
// than a note: embeds render them, link resolution leaves them unresolved
// without counting them as broken (SPEC.md §Index schema).
inline bool isAttachmentTarget(const QString& target)
{
    static const QSet<QString> kExtensions{
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("gif"),
        QStringLiteral("svg"), QStringLiteral("webp"), QStringLiteral("bmp"), QStringLiteral("pdf"),
        QStringLiteral("mp3"), QStringLiteral("wav"), QStringLiteral("ogg"), QStringLiteral("m4a"),
        QStringLiteral("mp4"), QStringLiteral("webm"), QStringLiteral("mov"), QStringLiteral("canvas"),
    };
    return kExtensions.contains(QFileInfo(target).suffix().toLower());
}

} // namespace coeus
