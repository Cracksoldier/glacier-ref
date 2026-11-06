#pragma once

// Generated image fixtures for decoding tests. Everything is built in code so
// the repository holds no binary fixtures and every edge case is explicit.

#include <QBuffer>
#include <QByteArray>
#include <QColorSpace>
#include <QImage>
#include <QImageWriter>
#include <QtEndian>

#include <array>

namespace glacier::test {

inline quint32 crc32(QByteArrayView data)
{
    static const auto table = [] {
        std::array<quint32, 256> t{};
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    quint32 c = 0xFFFFFFFFu;
    for (char ch : data)
        c = table[(c ^ uchar(ch)) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

inline void appendBE32(QByteArray &b, quint32 v)
{
    char c[4];
    qToBigEndian(v, c);
    b.append(c, 4);
}

inline void appendLE32(QByteArray &b, quint32 v)
{
    char c[4];
    qToLittleEndian(v, c);
    b.append(c, 4);
}

inline void appendLE16(QByteArray &b, quint16 v)
{
    char c[2];
    qToLittleEndian(v, c);
    b.append(c, 2);
}

// 40x20 opaque blue image with a red 10x5 block in the top-left corner, so
// orientation changes are observable.
inline QImage orientationPattern()
{
    QImage image(40, 20, QImage::Format_RGB32);
    image.fill(QColor(0, 0, 255));
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 10; ++x)
            image.setPixel(x, y, qRgb(255, 0, 0));
    return image;
}

inline QByteArray encode(const QImage &image, const char *format, int quality = -1)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, format);
    if (quality >= 0)
        writer.setQuality(quality);
    writer.write(image);
    return bytes;
}

// Big-endian TIFF-structured EXIF block with only IFD0 Orientation = value.
inline QByteArray exifOrientation(quint16 value)
{
    QByteArray e("MM\0*", 4);
    appendBE32(e, 8);
    e.append(char(0)).append(char(1));                    // one entry
    e.append(char(0x01)).append(char(0x12));              // tag 0x0112
    e.append(char(0)).append(char(3));                    // SHORT
    appendBE32(e, 1);                                     // count
    e.append(char(value >> 8)).append(char(value & 0xFF)).append(2, '\0');
    appendBE32(e, 0);                                     // no next IFD
    return e;
}

inline QByteArray pngChunk(const QByteArray &type, const QByteArray &data)
{
    QByteArray chunk;
    appendBE32(chunk, quint32(data.size()));
    const QByteArray typed = type + data;
    chunk += typed;
    appendBE32(chunk, crc32(typed));
    return chunk;
}

inline QByteArray pngWithExif(QByteArray png, quint16 orientation)
{
    const qsizetype afterIhdr = 8 + 12 + 13;
    png.insert(afterIhdr, pngChunk("eXIf", exifOrientation(orientation)));
    return png;
}

inline QByteArray jpegWithExif(QByteArray jpeg, quint16 orientation)
{
    const QByteArray payload = QByteArray("Exif\0\0", 6) + exifOrientation(orientation);
    QByteArray segment;
    segment.append('\xFF').append('\xE1');
    const int length = int(payload.size()) + 2;
    segment.append(char(length >> 8)).append(char(length & 0xFF));
    segment += payload;
    jpeg.insert(2, segment); // right after SOI
    return jpeg;
}

// Converts a simple WebP (RIFF/WEBP/VP8 or VP8L) into the extended format with
// a VP8X header and an EXIF chunk.
inline QByteArray webpWithExif(const QByteArray &webp, QSize canvas, quint16 orientation)
{
    const QByteArray imageChunk = webp.mid(12);
    QByteArray vp8x("VP8X");
    appendLE32(vp8x, 10);
    vp8x.append(char(0x08)).append(3, '\0'); // EXIF flag
    const int w = canvas.width() - 1, h = canvas.height() - 1;
    for (int v : {w, h}) {
        vp8x.append(char(v & 0xFF)).append(char((v >> 8) & 0xFF)).append(char((v >> 16) & 0xFF));
    }
    const QByteArray exif = exifOrientation(orientation);
    QByteArray exifChunk("EXIF");
    appendLE32(exifChunk, quint32(exif.size()));
    exifChunk += exif;
    if (exif.size() % 2)
        exifChunk.append('\0');
    const QByteArray body = QByteArray("WEBP") + vp8x + imageChunk + exifChunk;
    QByteArray out("RIFF");
    appendLE32(out, quint32(body.size()));
    return out + body;
}

// PNG consisting of a valid signature and IHDR claiming width x height, with
// no image data: probes must reject it from the header alone.
inline QByteArray pngHeaderOnly(quint32 width, quint32 height)
{
    QByteArray ihdr;
    appendBE32(ihdr, width);
    appendBE32(ihdr, height);
    ihdr.append(char(8)).append(char(6)).append(3, '\0'); // 8-bit RGBA
    return QByteArray("\x89PNG\r\n\x1a\n", 8) + pngChunk("IHDR", ihdr) + pngChunk("IEND", {});
}

inline QByteArray gifFrames(int frames)
{
    QByteArray gif("GIF89a");
    gif.append("\x01\x00\x01\x00\x80\x00\x00", 7);           // 1x1, 2-colour table
    gif.append("\xff\x00\x00\x00\x00\x00", 6);               // red, black
    for (int i = 0; i < frames; ++i) {
        gif.append("\x21\xf9\x04\x00\x0a\x00\x00\x00", 8);   // graphic control, 100 ms
        gif.append("\x2c\x00\x00\x00\x00\x01\x00\x01\x00\x00", 10);
        gif.append("\x02\x02\x44\x01\x00", 5);               // LZW: one pixel, index 0
    }
    gif.append(';');
    return gif;
}

// Little-endian, uncompressed 8-bit grayscale 1x1 TIFF with `pages` IFDs.
inline QByteArray tiffPages(int pages)
{
    constexpr int entries = 8;
    constexpr int ifdSize = 2 + entries * 12 + 4;
    QByteArray t("II*\0", 4);
    appendLE32(t, 8);
    qsizetype ifdOffset = 8;
    for (int page = 0; page < pages; ++page) {
        const quint32 pixelOffset = quint32(ifdOffset + ifdSize);
        const quint32 nextIfd = page + 1 < pages ? quint32((pixelOffset + 1 + 1) & ~1u) : 0;
        auto entry = [&t](quint16 tag, quint16 type, quint32 value) {
            appendLE16(t, tag);
            appendLE16(t, type);
            appendLE32(t, 1);
            if (type == 3) {
                appendLE16(t, quint16(value));
                appendLE16(t, 0);
            } else {
                appendLE32(t, value);
            }
        };
        appendLE16(t, entries);
        entry(256, 3, 1);           // ImageWidth
        entry(257, 3, 1);           // ImageLength
        entry(258, 3, 8);           // BitsPerSample
        entry(259, 3, 1);           // Compression: none
        entry(262, 3, 1);           // Photometric: BlackIsZero
        entry(273, 4, pixelOffset); // StripOffsets
        entry(278, 3, 1);           // RowsPerStrip
        entry(279, 4, 1);           // StripByteCounts
        appendLE32(t, nextIfd);
        t.append('\x80');       // the pixel
        while (nextIfd && t.size() < qsizetype(nextIfd))
            t.append('\0');
        ifdOffset = nextIfd;
    }
    return t;
}

inline QByteArray psdHeader()
{
    QByteArray p("8BPS");
    p.append("\x00\x01", 2).append(6, '\0');            // version 1, reserved
    p.append("\x00\x03", 2);                            // channels
    p.append("\x00\x00\x00\x10\x00\x00\x00\x10", 8);    // 16x16
    p.append("\x00\x08\x00\x03", 4);                    // 8-bit RGB
    p.append(4 * 4, '\0');                              // empty sections
    return p;
}

} // namespace glacier::test
