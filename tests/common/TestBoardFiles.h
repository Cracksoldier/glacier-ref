#pragma once

// Boards for persistence tests and the compatibility fixtures: a session store
// in a temporary directory, real image assets, and a board that uses every
// feature of the format.

#include "TestImages.h"

#include "glacier/core/SessionAssetStore.h"
#include "glacier/io/ImageDecoder.h"
#include "glacier/persistence/Manifest.h"

#include <QColor>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTimeZone>

#include <algorithm>

#include <memory>

namespace glacier::test {

// A session store in its own temporary directory.
struct StoreFixture
{
    QTemporaryDir root;
    std::shared_ptr<SessionAssetStore> store;

    StoreFixture()
    {
        SessionAssetStore::Options options;
        options.rootDir = root.path();
        store = SessionAssetStore::create(options);
    }
};

// Deterministic IDs, so fixtures and expectations can name entities.
inline Id fixedId(int n)
{
    return *Id::fromString(QStringLiteral("00000000-0000-4000-8000-%1").arg(n, 12, 10, QLatin1Char('0')));
}

// A solid image with a white pixel at the top-left corner (orientation marker).
inline QImage markedImage(QSize size, QColor color)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(color);
    image.setPixel(0, 0, qRgb(255, 255, 255));
    return image;
}

inline QByteArray svgBytes()
{
    return QByteArrayLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" height=\"30\">"
                             "<rect width=\"40\" height=\"30\" fill=\"#2a7\"/></svg>");
}

// Probes `bytes`, puts them into the store, and describes them as an asset.
inline Asset storeAsset(SessionAssetStore &store, const QByteArray &bytes, Id id = Id::generate())
{
    const io::ProbeOutcome probe = io::probeImage(bytes);
    const auto put = store.put(bytes);
    if (!probe.info || !put)
        return {};
    return Asset{id, put->sha256, io::mediaType(probe.info->format), probe.info->orientedSize, bytes.size(), false, {}};
}

// A board with PNG, JPEG, BMP and SVG assets (one shared by two items), a
// cropped + rotated + flipped image, a group with a rotated transform, notes
// with Unicode text and custom colours, a locked item, a custom background
// and grayscale. `linkedFile`, if set, adds a linked asset for that file.
inline persistence::BoardContent richBoard(SessionAssetStore &store, const QString &linkedFile = {})
{
    persistence::BoardContent c;
    c.metadata.documentId = fixedId(1);
    c.metadata.title = QStringLiteral("Référence — 参考 board");
    c.metadata.created = QDateTime(QDate(2026, 11, 7), QTime(10, 0, 0, 250), QTimeZone::UTC);
    c.metadata.modified = QDateTime(QDate(2026, 11, 7), QTime(11, 30, 12, 345), QTimeZone::UTC);

    const Asset png = storeAsset(store, encode(markedImage({120, 80}, QColor(200, 30, 30)), "png"), fixedId(10));
    const Asset jpeg = storeAsset(store, encode(markedImage({64, 48}, QColor(30, 30, 200)), "jpeg", 95), fixedId(11));
    const Asset bmp = storeAsset(store, encode(markedImage({16, 16}, QColor(30, 200, 30)), "bmp"), fixedId(12));
    const Asset svg = storeAsset(store, svgBytes(), fixedId(13));
    c.snapshot.assets = {png, jpeg, bmp, svg};

    ImageItemData a;
    a.id = fixedId(20);
    a.assetId = png.id;
    a.displaySize = QSizeF(png.orientedSize);
    a.crop = QRectF(0.125, 0.25, 0.5, 0.5);
    // Flipped horizontally, rotated 30 degrees, scaled 1.5 and moved.
    a.transform = Affine2D{-1.299038105676658, 0.75, 0.75, 1.299038105676658, 400.25, -120.5};
    a.z = 1;
    c.snapshot.images.append(a);

    ImageItemData b;
    b.id = fixedId(21);
    b.assetId = png.id; // shares the asset
    b.displaySize = QSizeF(240, 160);
    b.transform = Affine2D::translation(-300, 50);
    b.z = 2;
    b.locked = true;
    c.snapshot.images.append(b);

    ImageItemData m1;
    m1.id = fixedId(22);
    m1.assetId = jpeg.id;
    m1.displaySize = QSizeF(jpeg.orientedSize);
    m1.transform = Affine2D::translation(0, 0);
    m1.groupId = fixedId(40);
    c.snapshot.images.append(m1);

    ImageItemData m2;
    m2.id = fixedId(23);
    m2.assetId = bmp.id;
    m2.displaySize = QSizeF(bmp.orientedSize);
    m2.transform = Affine2D::translation(80, 10);
    m2.groupId = fixedId(40);
    c.snapshot.images.append(m2);

    ImageItemData s;
    s.id = fixedId(24);
    s.assetId = svg.id;
    s.displaySize = QSizeF(svg.orientedSize);
    s.transform = Affine2D{1, 0, 0, -1, 600, 300}; // flipped vertically
    s.z = 4;
    c.snapshot.images.append(s);

    NoteData note;
    note.id = fixedId(30);
    note.text = QStringLiteral("Lighting: warm key, cool rim.\nÄÖÜ ß — 日本語 ✓ 🎨");
    note.width = 260.5;
    note.height = 88.25;
    note.fontSize = 18;
    note.textColor = 0xFF102030u;
    note.backgroundColor = 0xE0FFF1B0u;
    note.transform = Affine2D{0.9659258262890683, 0.25881904510252074, -0.25881904510252074, 0.9659258262890683, 50, 400};
    note.z = 5;
    c.snapshot.notes.append(note);

    NoteData member;
    member.id = fixedId(31);
    member.text = QStringLiteral("group note");
    member.transform = Affine2D::translation(0, 60);
    member.groupId = fixedId(40);
    c.snapshot.notes.append(member);

    GroupData group;
    group.id = fixedId(40);
    group.name = QStringLiteral("Palette ✓");
    group.transform = Affine2D{0, 1, -1, 0, 900, 100}; // rotated 90 degrees
    group.children = {m1.id, member.id, m2.id};
    group.z = 3;
    c.snapshot.groups.append(group);

    if (!linkedFile.isEmpty()) {
        QFile file(linkedFile);
        const QByteArray bytes = file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        Asset linked = storeAsset(store, bytes, fixedId(14));
        linked.linked = true;
        linked.linkPath = linkedFile;
        c.snapshot.assets.append(linked);
        ImageItemData l;
        l.id = fixedId(25);
        l.assetId = linked.id;
        l.displaySize = QSizeF(linked.orientedSize);
        l.transform = Affine2D::translation(-500, -500);
        l.z = 6;
        c.snapshot.images.append(l);
    }

    c.view.backgroundColor = 0xFF20242Au;
    c.view.grayscale = true;
    c.viewCenter = QPointF(123.5, -45.25);
    c.zoom = 0.75;
    return c;
}

// The content in a canonical order, for comparisons.
inline persistence::BoardContent sorted(persistence::BoardContent c)
{
    auto byId = [](auto &list) {
        std::sort(list.begin(), list.end(), [](const auto &x, const auto &y) { return x.id.toString() < y.id.toString(); });
    };
    byId(c.snapshot.assets);
    byId(c.snapshot.images);
    byId(c.snapshot.notes);
    byId(c.snapshot.groups);
    return c;
}

} // namespace glacier::test
