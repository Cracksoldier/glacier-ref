// Proves the ZIP-library selection criteria (spec §5) against the vendored
// miniz: archives are written through callbacks to a QIODevice (as QSaveFile
// will be), entries can be stored or deflated, single entries are read as
// streams, entries can be copied raw into a new archive, and corruption is
// detected.

#include "GlacierTestMain.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QObject>

#include <miniz.h>

#include <cstring>

namespace {

size_t writeToDevice(void *opaque, mz_uint64 offset, const void *data, size_t size)
{
    auto *device = static_cast<QIODevice *>(opaque);
    if (device->pos() != static_cast<qint64>(offset) && !device->seek(static_cast<qint64>(offset)))
        return 0;
    const qint64 written = device->write(static_cast<const char *>(data), static_cast<qint64>(size));
    return written < 0 ? 0 : static_cast<size_t>(written);
}

size_t readFromDevice(void *opaque, mz_uint64 offset, void *data, size_t size)
{
    auto *device = static_cast<QIODevice *>(opaque);
    if (!device->seek(static_cast<qint64>(offset)))
        return 0;
    const qint64 read = device->read(static_cast<char *>(data), static_cast<qint64>(size));
    return read < 0 ? 0 : static_cast<size_t>(read);
}

const QByteArray &manifestBytes()
{
    static const QByteArray bytes = QByteArray(R"({"format":"glacier-ref","schemaVersion":1})").repeated(64);
    return bytes;
}

const QByteArray &assetBytes()
{
    static const QByteArray bytes = [] {
        QByteArray b(4096, '\0');
        for (int i = 0; i < b.size(); ++i)
            b[i] = static_cast<char>((i * 131) & 0xff);
        return b;
    }();
    return bytes;
}

bool writeArchive(QIODevice &device)
{
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    zip.m_pWrite = &writeToDevice;
    zip.m_pIO_opaque = &device;
    if (!mz_zip_writer_init(&zip, 0))
        return false;

    bool ok = mz_zip_writer_add_mem(&zip, "manifest.json", manifestBytes().constData(),
                                    static_cast<size_t>(manifestBytes().size()), MZ_DEFAULT_LEVEL)
           && mz_zip_writer_add_mem(&zip, "assets/0123.bin", assetBytes().constData(),
                                    static_cast<size_t>(assetBytes().size()), MZ_NO_COMPRESSION)
           && mz_zip_writer_finalize_archive(&zip);
    ok = mz_zip_writer_end(&zip) && ok;
    return ok;
}

struct Reader
{
    mz_zip_archive zip;
    bool open = false;

    explicit Reader(QIODevice &device)
    {
        mz_zip_zero_struct(&zip);
        zip.m_pRead = &readFromDevice;
        zip.m_pIO_opaque = &device;
        open = mz_zip_reader_init(&zip, static_cast<mz_uint64>(device.size()), 0);
    }
    ~Reader()
    {
        if (open)
            mz_zip_reader_end(&zip);
    }
    Reader(const Reader &) = delete;
    Reader &operator=(const Reader &) = delete;
};

// Streams one entry in small chunks. Returns false on any error, including a
// CRC mismatch (checked by mz_zip_reader_extract_iter_free).
bool streamEntry(mz_zip_archive &zip, int index, QByteArray &out)
{
    mz_zip_reader_extract_iter_state *iter =
        mz_zip_reader_extract_iter_new(&zip, static_cast<mz_uint>(index), 0);
    if (!iter)
        return false;
    char chunk[7];
    for (;;) {
        const size_t n = mz_zip_reader_extract_iter_read(iter, chunk, sizeof chunk);
        if (n == 0)
            break;
        out.append(chunk, static_cast<qsizetype>(n));
    }
    return mz_zip_reader_extract_iter_free(iter);
}

} // namespace

class TestZip : public QObject
{
    Q_OBJECT

private slots:
    void writeAndStreamRead()
    {
        QBuffer buffer;
        QVERIFY(buffer.open(QIODevice::ReadWrite));
        QVERIFY(writeArchive(buffer));

        Reader reader(buffer);
        QVERIFY(reader.open);
        QCOMPARE(mz_zip_reader_get_num_files(&reader.zip), 2u);

        const int manifest = mz_zip_reader_locate_file(&reader.zip, "manifest.json", nullptr, 0);
        const int asset = mz_zip_reader_locate_file(&reader.zip, "assets/0123.bin", nullptr, 0);
        QVERIFY(manifest >= 0);
        QVERIFY(asset >= 0);

        mz_zip_archive_file_stat stat;
        QVERIFY(mz_zip_reader_file_stat(&reader.zip, static_cast<mz_uint>(manifest), &stat));
        QCOMPARE(stat.m_method, static_cast<mz_uint16>(MZ_DEFLATED));
        QVERIFY(stat.m_comp_size < stat.m_uncomp_size);
        QCOMPARE(stat.m_crc32, static_cast<mz_uint32>(mz_crc32(MZ_CRC32_INIT,
            reinterpret_cast<const unsigned char *>(manifestBytes().constData()),
            static_cast<size_t>(manifestBytes().size()))));

        QVERIFY(mz_zip_reader_file_stat(&reader.zip, static_cast<mz_uint>(asset), &stat));
        QCOMPARE(stat.m_method, static_cast<mz_uint16>(0)); // stored
        QCOMPARE(stat.m_comp_size, stat.m_uncomp_size);

        QByteArray streamed;
        QVERIFY(streamEntry(reader.zip, manifest, streamed));
        QCOMPARE(streamed, manifestBytes());

        streamed.clear();
        QVERIFY(streamEntry(reader.zip, asset, streamed));
        QCOMPARE(streamed, assetBytes());
    }

    void rawCopyIntoNewArchive()
    {
        QBuffer source;
        QVERIFY(source.open(QIODevice::ReadWrite));
        QVERIFY(writeArchive(source));
        Reader reader(source);
        QVERIFY(reader.open);

        QBuffer target;
        QVERIFY(target.open(QIODevice::ReadWrite));
        mz_zip_archive writer;
        mz_zip_zero_struct(&writer);
        writer.m_pWrite = &writeToDevice;
        writer.m_pIO_opaque = &target;
        QVERIFY(mz_zip_writer_init(&writer, 0));
        const mz_uint count = mz_zip_reader_get_num_files(&reader.zip);
        for (mz_uint i = 0; i < count; ++i)
            QVERIFY(mz_zip_writer_add_from_zip_reader(&writer, &reader.zip, i));
        QVERIFY(mz_zip_writer_finalize_archive(&writer));
        QVERIFY(mz_zip_writer_end(&writer));

        Reader copied(target);
        QVERIFY(copied.open);
        const int manifest = mz_zip_reader_locate_file(&copied.zip, "manifest.json", nullptr, 0);
        QVERIFY(manifest >= 0);
        QByteArray streamed;
        QVERIFY(streamEntry(copied.zip, manifest, streamed));
        QCOMPARE(streamed, manifestBytes());
    }

    void detectsCorruptedEntry()
    {
        QBuffer buffer;
        QVERIFY(buffer.open(QIODevice::ReadWrite));
        QVERIFY(writeArchive(buffer));

        // Flip one byte inside the stored asset payload: the archive structure
        // stays valid, so only the CRC check can catch it.
        QByteArray bytes = buffer.data();
        const qsizetype payload = bytes.indexOf(assetBytes().left(64));
        QVERIFY(payload > 0);
        bytes[payload + 100] = static_cast<char>(bytes[payload + 100] ^ 0x5a);

        QBuffer corrupted(&bytes);
        QVERIFY(corrupted.open(QIODevice::ReadOnly));
        Reader reader(corrupted);
        QVERIFY(reader.open);
        const int asset = mz_zip_reader_locate_file(&reader.zip, "assets/0123.bin", nullptr, 0);
        QVERIFY(asset >= 0);
        QByteArray streamed;
        QVERIFY(!streamEntry(reader.zip, asset, streamed));
    }

    void rejectsGarbage()
    {
        QByteArray garbage(1024, 'x');
        QBuffer buffer(&garbage);
        QVERIFY(buffer.open(QIODevice::ReadOnly));
        Reader reader(buffer);
        QVERIFY(!reader.open);
    }
};

GLACIER_TEST_CORE_MAIN(TestZip)
#include "tst_zip.moc"
