#include "glacier/editing/ClipboardFragment.h"

#include "glacier/core/SessionAssetStore.h"

#include <QCborArray>
#include <QCborMap>
#include <QCborValue>

#include <cmath>

using namespace Qt::StringLiterals;

namespace glacier::editing {

namespace {

const QString FormatName = u"glacier-ref-items"_s;
constexpr int FormatVersion = 1;

QCborValue idValue(const Id &id)
{
    return id.isNull() ? QCborValue(QString()) : QCborValue(id.toString());
}

QCborArray transformValue(const Affine2D &t)
{
    return QCborArray{t.m11, t.m12, t.m21, t.m22, t.dx, t.dy};
}

// --- strict readers --------------------------------------------------------

struct Reader
{
    QString error;

    bool failed() const { return !error.isEmpty(); }

    void fail(const QString &message)
    {
        if (error.isEmpty())
            error = message;
    }

    double number(const QCborValue &v, const char *field)
    {
        if (!v.isDouble() && !v.isInteger()) {
            fail(u"field \"%1\" must be a number"_s.arg(QString::fromLatin1(field)));
            return 0.0;
        }
        const double d = v.toDouble();
        if (!std::isfinite(d))
            fail(u"field \"%1\" must be finite"_s.arg(QString::fromLatin1(field)));
        return d;
    }

    QString string(const QCborValue &v, const char *field, qsizetype maxLength)
    {
        if (!v.isString()) {
            fail(u"field \"%1\" must be a string"_s.arg(QString::fromLatin1(field)));
            return {};
        }
        QString s = v.toString();
        if (s.size() > maxLength)
            fail(u"field \"%1\" is too long"_s.arg(QString::fromLatin1(field)));
        return s;
    }

    Id id(const QCborValue &v, const char *field, bool allowNull = false)
    {
        const QString s = string(v, field, 64);
        if (allowNull && s.isEmpty())
            return {};
        const auto parsed = Id::fromString(s);
        if (!parsed) {
            fail(u"field \"%1\" is not a valid ID"_s.arg(QString::fromLatin1(field)));
            return {};
        }
        return *parsed;
    }

    bool boolean(const QCborValue &v, const char *field)
    {
        if (!v.isBool())
            fail(u"field \"%1\" must be a boolean"_s.arg(QString::fromLatin1(field)));
        return v.toBool();
    }

    quint32 color(const QCborValue &v, const char *field)
    {
        if (!v.isInteger() || v.toInteger() < 0 || v.toInteger() > 0xFFFFFFFFll) {
            fail(u"field \"%1\" must be an ARGB colour"_s.arg(QString::fromLatin1(field)));
            return 0;
        }
        return quint32(v.toInteger());
    }

    Affine2D transform(const QCborValue &v)
    {
        const QCborArray a = v.toArray();
        if (!v.isArray() || a.size() != 6) {
            fail(u"transform must have six coefficients"_s);
            return {};
        }
        return {number(a[0], "t"), number(a[1], "t"), number(a[2], "t"),
                number(a[3], "t"), number(a[4], "t"), number(a[5], "t")};
    }

    QCborArray array(const QCborMap &map, const QString &key, qsizetype maxSize)
    {
        const QCborValue v = map.value(key);
        if (v.isUndefined())
            return {};
        if (!v.isArray()) {
            fail(u"\"%1\" must be an array"_s.arg(key));
            return {};
        }
        QCborArray a = v.toArray();
        if (a.size() > maxSize)
            fail(u"\"%1\" has too many entries"_s.arg(key));
        return a;
    }
};

} // namespace

QByteArray encodeFragment(const ClipboardFragment &fragment)
{
    QCborArray assets;
    for (const Asset &a : fragment.snapshot.assets) {
        assets.append(QCborMap{
            {u"id"_s, idValue(a.id)},
            {u"sha256"_s, QString::fromLatin1(a.sha256)},
            {u"type"_s, a.mediaType},
            {u"w"_s, a.orientedSize.width()},
            {u"h"_s, a.orientedSize.height()},
            {u"len"_s, a.byteLength},
            {u"bytes"_s, fragment.assetBytes.value(a.sha256)},
        });
    }
    QCborArray images;
    for (const ImageItemData &i : fragment.snapshot.images) {
        images.append(QCborMap{
            {u"id"_s, idValue(i.id)},
            {u"asset"_s, idValue(i.assetId)},
            {u"t"_s, transformValue(i.transform)},
            {u"dw"_s, i.displaySize.width()},
            {u"dh"_s, i.displaySize.height()},
            {u"crop"_s, QCborArray{i.crop.x(), i.crop.y(), i.crop.width(), i.crop.height()}},
            {u"z"_s, i.z},
            {u"locked"_s, i.locked},
            {u"group"_s, idValue(i.groupId)},
        });
    }
    QCborArray notes;
    for (const NoteData &n : fragment.snapshot.notes) {
        notes.append(QCborMap{
            {u"id"_s, idValue(n.id)},
            {u"text"_s, n.text},
            {u"w"_s, n.width},
            {u"h"_s, n.height},
            {u"fs"_s, n.fontSize},
            {u"tc"_s, qint64(n.textColor)},
            {u"bc"_s, qint64(n.backgroundColor)},
            {u"t"_s, transformValue(n.transform)},
            {u"z"_s, n.z},
            {u"locked"_s, n.locked},
            {u"group"_s, idValue(n.groupId)},
        });
    }
    QCborArray groups;
    for (const GroupData &g : fragment.snapshot.groups) {
        QCborArray children;
        for (const Id &child : g.children)
            children.append(idValue(child));
        groups.append(QCborMap{
            {u"id"_s, idValue(g.id)},
            {u"name"_s, g.name},
            {u"t"_s, transformValue(g.transform)},
            {u"children"_s, children},
            {u"z"_s, g.z},
            {u"locked"_s, g.locked},
        });
    }
    const QCborMap root{
        {u"format"_s, FormatName},
        {u"version"_s, FormatVersion},
        {u"assets"_s, assets},
        {u"images"_s, images},
        {u"notes"_s, notes},
        {u"groups"_s, groups},
    };
    return QCborValue(root).toCbor();
}

std::optional<ClipboardFragment> decodeFragment(const QByteArray &data, const Limits &limits, QString *error)
{
    auto reject = [error](const QString &message) -> std::optional<ClipboardFragment> {
        if (error)
            *error = message;
        return std::nullopt;
    };
    if (data.isEmpty() || data.size() > ClipboardFragment::MaxTotalBytes + 16 * 1024 * 1024)
        return reject(u"clipboard data is empty or too large"_s);

    QCborParserError parseError;
    const QCborValue rootValue = QCborValue::fromCbor(data, &parseError);
    if (parseError.error != QCborError::NoError || !rootValue.isMap())
        return reject(u"clipboard data is not a Glacier Ref fragment"_s);
    const QCborMap root = rootValue.toMap();
    if (root.value(u"format"_s).toString() != FormatName)
        return reject(u"clipboard data is not a Glacier Ref fragment"_s);
    if (root.value(u"version"_s).toInteger() != FormatVersion)
        return reject(u"unsupported clipboard fragment version"_s);

    Reader r;
    ClipboardFragment fragment;
    qint64 totalBytes = 0;

    for (const QCborValue &v : r.array(root, u"assets"_s, limits.maxItems)) {
        const QCborMap m = v.toMap();
        Asset a;
        a.id = r.id(m.value(u"id"_s), "id");
        a.sha256 = r.string(m.value(u"sha256"_s), "sha256", 64).toLatin1();
        a.mediaType = r.string(m.value(u"type"_s), "type", 64);
        a.orientedSize = QSize(int(r.number(m.value(u"w"_s), "w")), int(r.number(m.value(u"h"_s), "h")));
        a.byteLength = qint64(r.number(m.value(u"len"_s), "len"));
        const QCborValue bytesValue = m.value(u"bytes"_s);
        if (!bytesValue.isByteArray())
            r.fail(u"asset bytes are missing"_s);
        const QByteArray bytes = bytesValue.toByteArray();
        if (bytes.size() > limits.maxAssetBytes)
            r.fail(u"an asset exceeds the size limit"_s);
        totalBytes += bytes.size();
        if (totalBytes > ClipboardFragment::MaxTotalBytes)
            r.fail(u"clipboard data is too large"_s);
        if (r.failed())
            return reject(r.error);
        if (!BoardDocument::isValidDigest(a.sha256) || SessionAssetStore::sha256Hex(bytes) != a.sha256)
            return reject(u"asset bytes do not match their digest"_s);
        if (bytes.size() != a.byteLength)
            return reject(u"asset length does not match its bytes"_s);
        fragment.assetBytes.insert(a.sha256, bytes);
        fragment.snapshot.assets.append(a);
    }

    for (const QCborValue &v : r.array(root, u"images"_s, limits.maxItems)) {
        const QCborMap m = v.toMap();
        ImageItemData i;
        i.id = r.id(m.value(u"id"_s), "id");
        i.assetId = r.id(m.value(u"asset"_s), "asset");
        i.transform = r.transform(m.value(u"t"_s));
        i.displaySize = QSizeF(r.number(m.value(u"dw"_s), "dw"), r.number(m.value(u"dh"_s), "dh"));
        const QCborArray crop = m.value(u"crop"_s).toArray();
        if (crop.size() != 4)
            r.fail(u"crop must have four values"_s);
        else
            i.crop = QRectF(r.number(crop[0], "crop"), r.number(crop[1], "crop"), r.number(crop[2], "crop"),
                            r.number(crop[3], "crop"));
        i.z = r.number(m.value(u"z"_s), "z");
        i.locked = r.boolean(m.value(u"locked"_s), "locked");
        i.groupId = r.id(m.value(u"group"_s), "group", true);
        fragment.snapshot.images.append(i);
    }

    for (const QCborValue &v : r.array(root, u"notes"_s, limits.maxItems)) {
        const QCborMap m = v.toMap();
        NoteData n;
        n.id = r.id(m.value(u"id"_s), "id");
        n.text = r.string(m.value(u"text"_s), "text", limits.maxNoteCharacters);
        n.width = r.number(m.value(u"w"_s), "w");
        n.height = r.number(m.value(u"h"_s), "h");
        n.fontSize = r.number(m.value(u"fs"_s), "fs");
        n.textColor = r.color(m.value(u"tc"_s), "tc");
        n.backgroundColor = r.color(m.value(u"bc"_s), "bc");
        n.transform = r.transform(m.value(u"t"_s));
        n.z = r.number(m.value(u"z"_s), "z");
        n.locked = r.boolean(m.value(u"locked"_s), "locked");
        n.groupId = r.id(m.value(u"group"_s), "group", true);
        fragment.snapshot.notes.append(n);
    }

    for (const QCborValue &v : r.array(root, u"groups"_s, limits.maxItems)) {
        const QCborMap m = v.toMap();
        GroupData g;
        g.id = r.id(m.value(u"id"_s), "id");
        g.name = r.string(m.value(u"name"_s), "name", 256);
        g.transform = r.transform(m.value(u"t"_s));
        for (const QCborValue &child : r.array(m, u"children"_s, limits.maxItems))
            g.children.append(r.id(child, "children"));
        g.z = r.number(m.value(u"z"_s), "z");
        g.locked = r.boolean(m.value(u"locked"_s), "locked");
        fragment.snapshot.groups.append(g);
    }

    if (r.failed())
        return reject(r.error);
    if (fragment.snapshot.images.size() + fragment.snapshot.notes.size() > limits.maxItems)
        return reject(u"clipboard fragment has too many items"_s);
    return fragment;
}

} // namespace glacier::editing
