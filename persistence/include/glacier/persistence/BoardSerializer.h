#pragma once

#include "glacier/core/Limits.h"
#include "glacier/persistence/Manifest.h"

#include <QByteArray>
#include <QHash>
#include <QSize>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>

class QIODevice;

namespace glacier {
class SessionAssetStore;
}

namespace glacier::persistence {

// Saving and loading .glacierref boards (spec §5, M3.02-M3.05). The functions
// are synchronous and thread-safe; the app runs them on worker threads with
// value inputs (an immutable snapshot) and the shared session store.

// Size and modification time of a file, to detect external modification.
struct FileFingerprint
{
    qint64 size = -1;          // -1: the file does not exist
    qint64 modifiedMsecs = 0;  // since the epoch, UTC

    bool exists() const { return size >= 0; }
    static FileFingerprint of(const QString &path);
    friend bool operator==(const FileFingerprint &, const FileFingerprint &) = default;
};

// Reports progress as (done, total) steps; called on the worker thread.
using ProgressFunction = std::function<void(int done, int total)>;

struct SaveRequest
{
    BoardContent content;     // metadata.modified is written as given
    QString path;             // target .glacierref file
    QByteArray previewPng;    // optional
    QString generator;
    std::shared_ptr<SessionAssetStore> store;
    Limits limits;
    // Failure injection (tests only): writes fail after this many bytes, as on
    // a full disk. Negative: disabled.
    qint64 failAfterBytes = -1;
};

struct SaveResult
{
    bool ok = false;
    QString error;
    FileFingerprint fingerprint; // of the written file
};

// Writes the archive to a sibling temporary file (QSaveFile) and replaces the
// target only after the archive is complete. On failure the previous file is
// untouched.
SaveResult saveBoard(const SaveRequest &request, const ProgressFunction &progress = {});

// Writes the archive to any device (used by saveBoard and tests).
bool writeBoardArchive(QIODevice &device, const SaveRequest &request, QString *error,
                       const ProgressFunction &progress = {});

// The state of a linked asset's file.
enum class LinkState { Available, Missing, Changed };

struct LinkCheck
{
    LinkState state = LinkState::Missing;
    QString reason;            // for Missing
    // For Changed: the file's current content (its bytes are in the store).
    QByteArray sha256;
    QString mediaType;
    QSize orientedSize;
    qint64 byteLength = 0;
};

// Reads a linked file (bounded by the asset limit), hashes it, and puts its
// bytes into the store. Available when the digest equals `expectedSha256`.
// Changed content is probed but never accepted here.
LinkCheck checkLink(const QString &absolutePath, const QByteArray &expectedSha256, SessionAssetStore &store,
                    const Limits &limits);

struct LoadResult
{
    std::optional<BoardContent> content; // set on success
    QString error;
    QHash<Id, LinkCheck> links;          // by asset ID, for linked assets
    QJsonObject manifest;                // after migration (extra members)
    FileFingerprint fingerprint;
};

// Opens, validates and loads a board: the manifest and every embedded asset
// are checked (limits, names, digests, formats, dimensions) and assets are
// streamed into the store before success is reported. The file is closed on
// return, so it can be replaced atomically while the board is open.
LoadResult loadBoard(const QString &path, const std::shared_ptr<SessionAssetStore> &store, const Limits &limits,
                     const ProgressFunction &progress = {});

// The same for any device; `boardDir` resolves relative links.
LoadResult loadBoardFromDevice(QIODevice &device, const QString &boardDir, SessionAssetStore &store,
                               const Limits &limits, const ProgressFunction &progress = {});

// The archive-level checks (entry names, counts, sizes, ratios), exposed for
// tests. Empty when the archive is acceptable.
class ZipReader;
QString checkArchive(const ZipReader &reader, const Limits &limits);

} // namespace glacier::persistence
