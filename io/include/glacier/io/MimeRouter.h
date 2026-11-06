#pragma once

#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>

class QMimeData;

namespace glacier::io {

struct ImportSource
{
    enum class Kind { File, ClipboardImage };
    Kind kind = Kind::File;
    QString filePath;   // Kind::File
    QImage image;       // Kind::ClipboardImage (encoded as PNG by the import job)
    QString displayName; // file name only, for user-facing reports
};

struct ImportPlan
{
    QList<ImportSource> sources;
    QStringList notices;   // skipped entries (folders, online links, ...)
    QString blockingMessage; // set when nothing can be imported
};

// The exact user-facing message for HTTP(S)-only input (spec §4).
QString onlineImagesNotSupportedMessage();

// Routes drag-and-drop or clipboard data (spec §4, offline boundary):
// local files/file: URLs win over image data (no duplicate import, original
// bytes preserved); otherwise image pixels; HTTP(S)-only content is refused.
ImportPlan routeMimeData(const QMimeData &mime);

// Plan for explicitly chosen paths (file dialog). Folders contribute their
// top-level image files; subfolders are skipped and reported.
ImportPlan planForPaths(const QStringList &paths);

} // namespace glacier::io
