#include "glacier/persistence/ZipArchive.h"

#include <QIODevice>

#include <miniz.h>

using namespace Qt::StringLiterals;

namespace glacier::persistence {

namespace {

size_t writeToDevice(void *opaque, mz_uint64 offset, const void *data, size_t size)
{
    auto *device = static_cast<QIODevice *>(opaque);
    if (device->pos() != static_cast<qint64>(offset) && !device->seek(static_cast<qint64>(offset)))
        return 0;
    const qint64 written = device->write(static_cast<const char *>(data), static_cast<qint64>(size));
    return written == static_cast<qint64>(size) ? size : 0;
}

size_t readFromDevice(void *opaque, mz_uint64 offset, void *data, size_t size)
{
    auto *device = static_cast<QIODevice *>(opaque);
    if (device->pos() != static_cast<qint64>(offset) && !device->seek(static_cast<qint64>(offset)))
        return 0;
    const qint64 read = device->read(static_cast<char *>(data), static_cast<qint64>(size));
    return read < 0 ? 0 : static_cast<size_t>(read);
}

QString zipError(mz_zip_archive &zip)
{
    return QString::fromLatin1(mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
}

} // namespace

// --- Writer ------------------------------------------------------------------

struct ZipWriter::Impl
{
    mz_zip_archive zip;
    bool open = false;
    bool finished = false;
};

ZipWriter::ZipWriter(QIODevice *device)
    : d(std::make_unique<Impl>())
{
    mz_zip_zero_struct(&d->zip);
    d->zip.m_pWrite = &writeToDevice;
    d->zip.m_pIO_opaque = device;
    d->open = mz_zip_writer_init_v2(&d->zip, 0, 0);
    if (!d->open)
        m_error = zipError(d->zip);
}

ZipWriter::~ZipWriter()
{
    if (d->open)
        mz_zip_writer_end(&d->zip);
}

bool ZipWriter::isOpen() const
{
    return d->open && !d->finished;
}

bool ZipWriter::add(const QString &name, const QByteArray &bytes, bool compress)
{
    if (!isOpen())
        return false;
    const QByteArray utf8 = name.toUtf8();
    const mz_uint flags = mz_uint(compress ? MZ_DEFAULT_LEVEL : MZ_NO_COMPRESSION) | mz_uint(MZ_ZIP_FLAG_ASCII_FILENAME);
    if (!mz_zip_writer_add_mem_ex(&d->zip, utf8.constData(), bytes.constData(), static_cast<size_t>(bytes.size()),
                                  nullptr, 0, flags, 0, 0)) {
        m_error = zipError(d->zip);
        return false;
    }
    return true;
}

bool ZipWriter::finish()
{
    if (!isOpen())
        return false;
    const bool ok = mz_zip_writer_finalize_archive(&d->zip);
    if (!ok)
        m_error = zipError(d->zip);
    d->finished = true;
    return ok;
}

// --- Reader ------------------------------------------------------------------

struct ZipReader::Impl
{
    mz_zip_archive zip;
    bool open = false;
};

ZipReader::ZipReader(QIODevice *device)
    : d(std::make_unique<Impl>())
{
    mz_zip_zero_struct(&d->zip);
    d->zip.m_pRead = &readFromDevice;
    d->zip.m_pIO_opaque = device;
    d->open = device && device->isReadable()
           && mz_zip_reader_init(&d->zip, static_cast<mz_uint64>(device->size()), 0);
    if (!d->open) {
        m_error = u"not a valid ZIP archive"_s;
        return;
    }
    const mz_uint count = mz_zip_reader_get_num_files(&d->zip);
    m_entries.reserve(static_cast<qsizetype>(count));
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&d->zip, i, &stat)) {
            m_error = u"the archive directory is damaged"_s;
            m_entries.clear();
            mz_zip_reader_end(&d->zip);
            d->open = false;
            return;
        }
        ZipEntry e;
        e.index = static_cast<int>(i);
        e.name = QString::fromUtf8(stat.m_filename);
        e.size = static_cast<qint64>(stat.m_uncomp_size);
        e.compressedSize = static_cast<qint64>(stat.m_comp_size);
        e.isDirectory = stat.m_is_directory;
        e.isEncrypted = stat.m_is_encrypted;
        e.isSupported = stat.m_is_supported && (stat.m_method == 0 || stat.m_method == MZ_DEFLATED);
        m_entries.append(e);
    }
}

ZipReader::~ZipReader()
{
    if (d->open)
        mz_zip_reader_end(&d->zip);
}

bool ZipReader::isOpen() const
{
    return d->open;
}

std::optional<ZipEntry> ZipReader::find(const QString &name) const
{
    for (const ZipEntry &e : m_entries)
        if (e.name == name)
            return e;
    return std::nullopt;
}

std::optional<QByteArray> ZipReader::read(const ZipEntry &entry, qint64 maxBytes, QString *error) const
{
    auto fail = [error](const QString &message) -> std::optional<QByteArray> {
        if (error)
            *error = message;
        return std::nullopt;
    };
    if (!d->open || entry.index < 0)
        return fail(u"the archive is not open"_s);
    if (entry.isEncrypted || !entry.isSupported || entry.isDirectory)
        return fail(u"the entry \"%1\" uses an unsupported ZIP feature"_s.arg(entry.name));
    if (entry.size > maxBytes)
        return fail(u"the entry \"%1\" is too large"_s.arg(entry.name));

    mz_zip_reader_extract_iter_state *iter =
        mz_zip_reader_extract_iter_new(&d->zip, static_cast<mz_uint>(entry.index), 0);
    if (!iter)
        return fail(u"the entry \"%1\" could not be read"_s.arg(entry.name));
    QByteArray out;
    out.reserve(entry.size);
    constexpr size_t Chunk = 256 * 1024;
    QByteArray chunk(Chunk, Qt::Uninitialized);
    bool tooLong = false;
    for (;;) {
        const size_t n = mz_zip_reader_extract_iter_read(iter, chunk.data(), Chunk);
        if (n == 0)
            break;
        if (out.size() + static_cast<qint64>(n) > entry.size) {
            tooLong = true;
            break;
        }
        out.append(chunk.constData(), static_cast<qsizetype>(n));
    }
    // Frees the iterator and verifies the CRC and length.
    const bool ok = mz_zip_reader_extract_iter_free(iter) && !tooLong && out.size() == entry.size;
    if (!ok)
        return fail(u"the entry \"%1\" is damaged"_s.arg(entry.name));
    return out;
}

} // namespace glacier::persistence
