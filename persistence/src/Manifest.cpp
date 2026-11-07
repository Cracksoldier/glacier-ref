#include "glacier/persistence/Manifest.h"

#include "glacier/core/LinkPaths.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace glacier::persistence {

namespace {

constexpr double MinZoom = 0.01;
constexpr double MaxZoom = 64.0;

QString extensionFor(const QString &mediaType)
{
    static const QMap<QString, QString> map = {
        {u"image/png"_s, u"png"_s},   {u"image/jpeg"_s, u"jpg"_s}, {u"image/webp"_s, u"webp"_s},
        {u"image/bmp"_s, u"bmp"_s},   {u"image/gif"_s, u"gif"_s},  {u"image/tiff"_s, u"tiff"_s},
        {u"image/svg+xml"_s, u"svg"_s},
    };
    return map.value(mediaType);
}

QString colorString(quint32 argb)
{
    return u"#%1"_s.arg(argb, 8, 16, QLatin1Char('0')).toUpper();
}

QString timestamp(const QDateTime &t)
{
    return t.toUTC().toString(Qt::ISODateWithMs);
}

QJsonArray numbers(std::initializer_list<double> values)
{
    QJsonArray a;
    for (double v : values)
        a.append(v);
    return a;
}

QJsonArray transformArray(const Affine2D &t)
{
    return numbers({t.m11, t.m12, t.m21, t.m22, t.dx, t.dy});
}

// --- Strict decoding ---------------------------------------------------------

class Decoder
{
public:
    explicit Decoder(const Limits &limits) : m_limits(limits) {}

    bool ok() const { return m_error.isEmpty(); }
    QString error() const { return m_error; }

    void fail(const QString &message)
    {
        if (m_error.isEmpty())
            m_error = message;
    }

    QJsonValue member(const QJsonObject &o, const QString &key, const QString &where)
    {
        const QJsonValue v = o.value(key);
        if (v.isUndefined())
            fail(u"%1 is missing \"%2\""_s.arg(where, key));
        return v;
    }

    QString string(const QJsonObject &o, const QString &key, const QString &where, qsizetype maxLength)
    {
        const QJsonValue v = member(o, key, where);
        if (!ok())
            return {};
        if (!v.isString()) {
            fail(u"%1: \"%2\" must be a string"_s.arg(where, key));
            return {};
        }
        const QString s = v.toString();
        if (s.size() > maxLength)
            fail(u"%1: \"%2\" is too long"_s.arg(where, key));
        return s;
    }

    bool boolean(const QJsonObject &o, const QString &key, const QString &where)
    {
        const QJsonValue v = member(o, key, where);
        if (ok() && !v.isBool())
            fail(u"%1: \"%2\" must be true or false"_s.arg(where, key));
        return v.toBool();
    }

    double number(const QJsonValue &v, const QString &what)
    {
        if (!v.isDouble() || !std::isfinite(v.toDouble())) {
            fail(u"%1 must be a finite number"_s.arg(what));
            return 0.0;
        }
        return v.toDouble();
    }

    double number(const QJsonObject &o, const QString &key, const QString &where)
    {
        const QJsonValue v = member(o, key, where);
        return ok() ? number(v, u"%1: \"%2\""_s.arg(where, key)) : 0.0;
    }

    qint64 integer(const QJsonObject &o, const QString &key, const QString &where, qint64 min, qint64 max)
    {
        const double d = number(o, key, where);
        if (!ok())
            return 0;
        if (d != std::floor(d) || d < double(min) || d > double(max)) {
            fail(u"%1: \"%2\" must be a whole number between %3 and %4"_s.arg(where, key).arg(min).arg(max));
            return 0;
        }
        return static_cast<qint64>(d);
    }

    QList<double> numberArray(const QJsonObject &o, const QString &key, const QString &where, qsizetype count)
    {
        const QJsonValue v = member(o, key, where);
        if (!ok())
            return {};
        const QJsonArray a = v.toArray();
        if (!v.isArray() || a.size() != count) {
            fail(u"%1: \"%2\" must be an array of %3 numbers"_s.arg(where, key).arg(count));
            return {};
        }
        QList<double> out;
        for (const QJsonValue &n : a)
            out.append(number(n, u"%1: \"%2\""_s.arg(where, key)));
        return ok() ? out : QList<double>();
    }

    Id id(const QJsonValue &v, const QString &what)
    {
        const auto parsed = v.isString() ? Id::fromString(v.toString()) : std::nullopt;
        if (!parsed) {
            fail(u"%1 is not a valid ID"_s.arg(what));
            return {};
        }
        return *parsed;
    }

    Id id(const QJsonObject &o, const QString &key, const QString &where)
    {
        const QJsonValue v = member(o, key, where);
        return ok() ? id(v, u"%1: \"%2\""_s.arg(where, key)) : Id();
    }

    Affine2D transform(const QJsonObject &o, const QString &where)
    {
        const QList<double> n = numberArray(o, u"transform"_s, where, 6);
        if (!ok())
            return {};
        const Affine2D t{n[0], n[1], n[2], n[3], n[4], n[5]};
        if (!t.isInvertible())
            fail(u"%1: the transform is not invertible"_s.arg(where));
        return t;
    }

    quint32 color(const QJsonObject &o, const QString &key, const QString &where)
    {
        const QString s = string(o, key, where, 9);
        if (!ok())
            return 0;
        bool parsed = false;
        const quint32 value = s.size() == 9 && s.startsWith(u'#') ? s.mid(1).toUInt(&parsed, 16) : 0;
        if (!parsed)
            fail(u"%1: \"%2\" must be a colour like #FF3A3D41"_s.arg(where, key));
        return value;
    }

    QDateTime time(const QJsonObject &o, const QString &key, const QString &where)
    {
        const QString s = string(o, key, where, 64);
        if (!ok())
            return {};
        QDateTime t = QDateTime::fromString(s, Qt::ISODateWithMs);
        if (!t.isValid() || !s.endsWith(u'Z')) {
            fail(u"%1: \"%2\" must be a UTC timestamp"_s.arg(where, key));
            return {};
        }
        return t.toUTC();
    }

    QJsonArray array(const QJsonObject &o, const QString &key, qsizetype maxSize)
    {
        const QJsonValue v = member(o, key, u"the manifest"_s);
        if (!ok())
            return {};
        if (!v.isArray()) {
            fail(u"\"%1\" must be an array"_s.arg(key));
            return {};
        }
        const QJsonArray a = v.toArray();
        if (a.size() > maxSize)
            fail(u"\"%1\" has too many entries"_s.arg(key));
        return a;
    }

    QJsonObject object(const QJsonValue &v, const QString &what)
    {
        if (!v.isObject())
            fail(u"%1 must be an object"_s.arg(what));
        return v.toObject();
    }

    const Limits &limits() const { return m_limits; }

private:
    const Limits &m_limits;
    QString m_error;
};

Asset decodeAsset(Decoder &d, const QJsonObject &o, const QString &boardDir)
{
    const QString where = u"an asset"_s;
    Asset a;
    a.id = d.id(o, u"id"_s, where);
    const QString kind = d.string(o, u"kind"_s, where, 16);
    a.sha256 = d.string(o, u"sha256"_s, where, 64).toLatin1();
    a.mediaType = d.string(o, u"mediaType"_s, where, 64);
    const int maxSide = std::numeric_limits<int>::max();
    a.orientedSize = QSize(int(d.integer(o, u"width"_s, where, 1, maxSide)),
                           int(d.integer(o, u"height"_s, where, 1, maxSide)));
    a.byteLength = d.integer(o, u"byteLength"_s, where, 1, d.limits().maxAssetBytes);
    if (!d.ok())
        return a;
    if (!BoardDocument::isValidDigest(a.sha256))
        d.fail(u"an asset has an invalid SHA-256 digest"_s);
    else if (!BoardDocument::isSupportedMediaType(a.mediaType))
        d.fail(u"an asset has an unsupported media type"_s);
    if (!d.ok())
        return a;

    if (kind == u"embedded"_s) {
        const QString entry = d.string(o, u"entry"_s, where, 128);
        if (d.ok() && entry != assetEntryName(a.sha256, a.mediaType))
            d.fail(u"an embedded asset names an unexpected archive entry"_s);
    } else if (kind == u"linked"_s) {
        const QString path = d.string(o, u"path"_s, where, 4096);
        const QString pathKind = d.string(o, u"pathKind"_s, where, 16);
        if (!d.ok())
            return a;
        if (pathKind != u"relative"_s && pathKind != u"absolute"_s) {
            d.fail(u"a linked asset has an unknown path kind"_s);
            return a;
        }
        const QString resolved = link_paths::resolve({path, pathKind == u"relative"_s}, boardDir);
        if (resolved.isEmpty()) {
            d.fail(u"a linked asset has an invalid path"_s);
            return a;
        }
        a.linked = true;
        a.linkPath = resolved;
    } else {
        d.fail(u"an asset has an unknown kind"_s);
    }
    return a;
}

void decodeItem(Decoder &d, const QJsonObject &o, Snapshot &snap)
{
    const QString type = d.string(o, u"type"_s, u"an item"_s, 16);
    if (!d.ok())
        return;
    const QString where = type == u"note"_s ? u"a note"_s : u"an image item"_s;
    const Id id = d.id(o, u"id"_s, where);
    const Affine2D transform = d.transform(o, where);
    const double z = d.number(o, u"z"_s, where);
    const bool locked = d.boolean(o, u"locked"_s, where);
    Id groupId;
    if (o.contains(u"group"_s))
        groupId = d.id(o.value(u"group"_s), u"%1: \"group\""_s.arg(where));
    if (!d.ok())
        return;

    if (type == u"image"_s) {
        ImageItemData item;
        item.id = id;
        item.assetId = d.id(o, u"asset"_s, where);
        const QList<double> size = d.numberArray(o, u"size"_s, where, 2);
        const QList<double> crop = d.numberArray(o, u"crop"_s, where, 4);
        if (!d.ok())
            return;
        item.transform = transform;
        item.displaySize = QSizeF(size[0], size[1]);
        item.crop = QRectF(crop[0], crop[1], crop[2], crop[3]);
        item.z = z;
        item.locked = locked;
        item.groupId = groupId;
        snap.images.append(item);
    } else if (type == u"note"_s) {
        NoteData note;
        note.id = id;
        note.text = d.string(o, u"text"_s, where, d.limits().maxNoteCharacters);
        note.width = d.number(o, u"width"_s, where);
        note.height = d.number(o, u"height"_s, where);
        note.fontSize = d.number(o, u"fontSize"_s, where);
        note.textColor = d.color(o, u"textColor"_s, where);
        note.backgroundColor = d.color(o, u"backgroundColor"_s, where);
        note.transform = transform;
        note.z = z;
        note.locked = locked;
        note.groupId = groupId;
        if (d.ok())
            snap.notes.append(note);
    } else {
        d.fail(u"an item has an unknown type"_s);
    }
}

void decodeGroup(Decoder &d, const QJsonObject &o, Snapshot &snap)
{
    const QString where = u"a group"_s;
    GroupData g;
    g.id = d.id(o, u"id"_s, where);
    g.name = d.string(o, u"name"_s, where, 256);
    g.transform = d.transform(o, where);
    g.z = d.number(o, u"z"_s, where);
    g.locked = d.boolean(o, u"locked"_s, where);
    const QJsonValue children = d.member(o, u"children"_s, where);
    if (!d.ok())
        return;
    if (!children.isArray()) {
        d.fail(u"a group's \"children\" must be an array"_s);
        return;
    }
    for (const QJsonValue &child : children.toArray())
        g.children.append(d.id(child, u"a group member"_s));
    if (d.ok())
        snap.groups.append(g);
}

} // namespace

QString formatIdentifier()
{
    return u"glacier-ref"_s;
}

QString assetEntryName(const QByteArray &sha256, const QString &mediaType)
{
    return u"assets/%1.%2"_s.arg(QString::fromLatin1(sha256), extensionFor(mediaType));
}

QByteArray writeManifest(const BoardContent &content, const ManifestWriteOptions &options)
{
    const Snapshot &snap = content.snapshot;
    QSet<Id> referenced;
    for (const ImageItemData &i : snap.images)
        referenced.insert(i.assetId);

    QJsonArray assets;
    for (const Asset &a : snap.assets) {
        if (!referenced.contains(a.id))
            continue;
        QJsonObject o{
            {u"id"_s, a.id.toString()},
            {u"kind"_s, a.linked ? u"linked"_s : u"embedded"_s},
            {u"sha256"_s, QString::fromLatin1(a.sha256)},
            {u"mediaType"_s, a.mediaType},
            {u"width"_s, a.orientedSize.width()},
            {u"height"_s, a.orientedSize.height()},
            {u"byteLength"_s, double(a.byteLength)},
        };
        if (a.linked) {
            const link_paths::StoredPath stored = link_paths::toStored(a.linkPath, options.boardDir);
            o.insert(u"path"_s, stored.path);
            o.insert(u"pathKind"_s, stored.relative ? u"relative"_s : u"absolute"_s);
        } else {
            o.insert(u"entry"_s, assetEntryName(a.sha256, a.mediaType));
        }
        assets.append(o);
    }

    QJsonArray items;
    auto common = [](QJsonObject &o, const Id &id, const char *type, const Affine2D &t, double z, bool locked,
                     const Id &group) {
        o.insert(u"id"_s, id.toString());
        o.insert(u"type"_s, QString::fromLatin1(type));
        o.insert(u"transform"_s, transformArray(t));
        o.insert(u"z"_s, z);
        o.insert(u"locked"_s, locked);
        if (!group.isNull())
            o.insert(u"group"_s, group.toString());
    };
    for (const ImageItemData &i : snap.images) {
        QJsonObject o;
        common(o, i.id, "image", i.transform, i.z, i.locked, i.groupId);
        o.insert(u"asset"_s, i.assetId.toString());
        o.insert(u"size"_s, numbers({i.displaySize.width(), i.displaySize.height()}));
        o.insert(u"crop"_s, numbers({i.crop.x(), i.crop.y(), i.crop.width(), i.crop.height()}));
        items.append(o);
    }
    for (const NoteData &n : snap.notes) {
        QJsonObject o;
        common(o, n.id, "note", n.transform, n.z, n.locked, n.groupId);
        o.insert(u"text"_s, n.text);
        o.insert(u"width"_s, n.width);
        o.insert(u"height"_s, n.height);
        o.insert(u"fontSize"_s, n.fontSize);
        o.insert(u"textColor"_s, colorString(n.textColor));
        o.insert(u"backgroundColor"_s, colorString(n.backgroundColor));
        items.append(o);
    }

    QJsonArray groups;
    for (const GroupData &g : snap.groups) {
        QJsonArray children;
        for (const Id &c : g.children)
            children.append(c.toString());
        groups.append(QJsonObject{
            {u"id"_s, g.id.toString()},
            {u"name"_s, g.name},
            {u"transform"_s, transformArray(g.transform)},
            {u"children"_s, children},
            {u"z"_s, g.z},
            {u"locked"_s, g.locked},
        });
    }

    QJsonObject root = options.extra;
    root.insert(u"format"_s, formatIdentifier());
    root.insert(u"schemaVersion"_s, CurrentSchemaVersion);
    root.insert(u"documentId"_s, content.metadata.documentId.toString());
    root.insert(u"title"_s, content.metadata.title);
    root.insert(u"created"_s, timestamp(content.metadata.created));
    root.insert(u"modified"_s, timestamp(content.metadata.modified));
    if (!options.generator.isEmpty())
        root.insert(u"generator"_s, options.generator);
    root.insert(u"assets"_s, assets);
    root.insert(u"items"_s, items);
    root.insert(u"groups"_s, groups);
    root.insert(u"view"_s, QJsonObject{
                               {u"background"_s, colorString(content.view.backgroundColor)},
                               {u"grayscale"_s, content.view.grayscale},
                               {u"center"_s, numbers({content.viewCenter.x(), content.viewCenter.y()})},
                               {u"zoom"_s, content.zoom},
                           });
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

const MigrationTable &builtInMigrations()
{
    static const MigrationTable table; // schema 1 is the first version
    return table;
}

bool migrateManifest(QJsonObject &manifest, const MigrationTable &migrations, int targetVersion, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    const QJsonValue v = manifest.value(u"schemaVersion"_s);
    const double d = v.toDouble(-1);
    if (!v.isDouble() || d != std::floor(d) || d < 0 || d > 1e6)
        return fail(u"the manifest has no valid schema version"_s);
    int version = int(d);
    if (version > targetVersion)
        return fail(u"This board was made by a newer version of Glacier Ref (format version %1). "
                    "Update Glacier Ref to open it."_s.arg(version));
    while (version < targetVersion) {
        const auto step = migrations.constFind(version);
        if (step == migrations.cend())
            return fail(u"the board uses format version %1, which this version of Glacier Ref cannot read"_s
                            .arg(version));
        QString stepError;
        if (!(*step)(manifest, &stepError))
            return fail(u"the board could not be upgraded from format version %1: %2"_s.arg(version).arg(stepError));
        ++version;
        manifest.insert(u"schemaVersion"_s, version);
    }
    return true;
}

int jsonNestingDepth(const QByteArray &json)
{
    int depth = 0;
    int deepest = 0;
    bool inString = false;
    bool escaped = false;
    for (const char c : json) {
        if (inString) {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                inString = false;
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '[' || c == '{') {
            deepest = std::max(deepest, ++depth);
        } else if (c == ']' || c == '}') {
            --depth;
        }
    }
    return deepest;
}

std::optional<ManifestReadResult> readManifest(const QByteArray &json, const QString &boardDir, const Limits &limits,
                                               QString *error, const MigrationTable &migrations, int currentVersion)
{
    auto fail = [error](const QString &message) -> std::optional<ManifestReadResult> {
        if (error)
            *error = message;
        return std::nullopt;
    };
    if (json.size() > limits.maxManifestBytes)
        return fail(u"the manifest is too large"_s);
    if (jsonNestingDepth(json) > limits.maxJsonDepth)
        return fail(u"the manifest is nested too deeply"_s);
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return fail(u"the manifest is not valid JSON"_s);
    QJsonObject root = doc.object();
    if (root.value(u"format"_s).toString() != formatIdentifier())
        return fail(u"the file is not a Glacier Ref board"_s);
    QString migrationError;
    if (!migrateManifest(root, migrations, currentVersion, &migrationError))
        return fail(migrationError);

    Decoder d(limits);
    ManifestReadResult result;
    BoardContent &c = result.content;
    const QString where = u"the manifest"_s;
    c.metadata.documentId = d.id(root, u"documentId"_s, where);
    c.metadata.title = d.string(root, u"title"_s, where, BoardDocument::MaxTitleLength);
    c.metadata.created = d.time(root, u"created"_s, where);
    c.metadata.modified = d.time(root, u"modified"_s, where);
    if (d.ok())
        if (const QString e = BoardDocument::validateTitle(c.metadata.title); !e.isEmpty())
            d.fail(e);

    const qsizetype maxEntries = limits.maxItems;
    for (const QJsonValue &v : d.array(root, u"assets"_s, maxEntries)) {
        const QJsonObject o = d.object(v, u"an asset"_s);
        if (!d.ok())
            break;
        c.snapshot.assets.append(decodeAsset(d, o, boardDir));
    }
    for (const QJsonValue &v : d.array(root, u"items"_s, maxEntries)) {
        const QJsonObject o = d.object(v, u"an item"_s);
        if (!d.ok())
            break;
        decodeItem(d, o, c.snapshot);
    }
    for (const QJsonValue &v : d.array(root, u"groups"_s, maxEntries)) {
        const QJsonObject o = d.object(v, u"a group"_s);
        if (!d.ok())
            break;
        decodeGroup(d, o, c.snapshot);
    }

    const QJsonObject view = d.object(d.member(root, u"view"_s, where), u"\"view\""_s);
    if (d.ok()) {
        const QString w = u"the view"_s;
        c.view.backgroundColor = d.color(view, u"background"_s, w);
        c.view.grayscale = d.boolean(view, u"grayscale"_s, w);
        const QList<double> center = d.numberArray(view, u"center"_s, w, 2);
        c.zoom = d.number(view, u"zoom"_s, w);
        if (d.ok()) {
            c.viewCenter = QPointF(center[0], center[1]);
            if ((c.view.backgroundColor >> 24) != 0xFFu)
                d.fail(u"the board background must be opaque"_s);
            else if (c.zoom < MinZoom || c.zoom > MaxZoom)
                d.fail(u"the view zoom is out of range"_s);
            else if (std::abs(center[0]) > Limits::CoordinateLimit || std::abs(center[1]) > Limits::CoordinateLimit)
                d.fail(u"the view centre is out of range"_s);
        }
    }
    if (!d.ok())
        return fail(d.error());

    if (const QString e = BoardDocument::validateSnapshot(c.snapshot, c.view, limits); !e.isEmpty())
        return fail(e);
    result.root = root;
    return result;
}

} // namespace glacier::persistence
