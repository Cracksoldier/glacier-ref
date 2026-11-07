#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <memory>
#include <optional>

class QIODevice;

namespace glacier::persistence {

// Minimal ZIP access over QIODevice callbacks (spec §5): miniz is built with
// MINIZ_NO_STDIO, so archives are never opened by path, and entries are never
// extracted to the filesystem. miniz stays out of the public headers.

class ZipWriter
{
public:
    // Writes a new archive to `device` (open for writing, positioned at 0).
    explicit ZipWriter(QIODevice *device);
    ~ZipWriter();
    ZipWriter(const ZipWriter &) = delete;
    ZipWriter &operator=(const ZipWriter &) = delete;

    bool isOpen() const;
    // Adds an entry; `compress` deflates it, otherwise it is stored.
    bool add(const QString &name, const QByteArray &bytes, bool compress);
    // Writes the central directory. The archive is complete only after this.
    bool finish();
    QString error() const { return m_error; }

private:
    struct Impl;
    std::unique_ptr<Impl> d;
    QString m_error;
};

struct ZipEntry
{
    int index = -1;
    QString name;           // as stored (UTF-8 decoded)
    qint64 size = 0;        // uncompressed, as declared
    qint64 compressedSize = 0;
    bool isDirectory = false;
    bool isEncrypted = false;
    bool isSupported = true; // stored or deflate, no unsupported features
};

class ZipReader
{
public:
    // Reads the central directory of the archive in `device` (open for reading).
    explicit ZipReader(QIODevice *device);
    ~ZipReader();
    ZipReader(const ZipReader &) = delete;
    ZipReader &operator=(const ZipReader &) = delete;

    bool isOpen() const;
    QString error() const { return m_error; }
    const QList<ZipEntry> &entries() const { return m_entries; }
    std::optional<ZipEntry> find(const QString &name) const;

    // Reads one entry, streaming it in chunks. Fails if the declared size
    // exceeds `maxBytes`, if the data is corrupt (CRC or length mismatch), or
    // if the entry uses an unsupported feature.
    std::optional<QByteArray> read(const ZipEntry &entry, qint64 maxBytes, QString *error = nullptr) const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
    QList<ZipEntry> m_entries;
    QString m_error;
};

} // namespace glacier::persistence
