#include "glacier/io/SvgValidator.h"

#include "glacier/io/ImageFormats.h"

#include <QBuffer>
#include <QImageReader>
#include <QSet>
#include <QXmlStreamReader>

using namespace Qt::StringLiterals;

namespace glacier::io {

namespace {

constexpr int MaxElements = 200000;
constexpr int MaxDepth = 256;

const QString SvgNs = u"http://www.w3.org/2000/svg"_s;
const QString XlinkNs = u"http://www.w3.org/1999/xlink"_s;

const QSet<QString> &allowedElements()
{
    static const QSet<QString> set = {
        // structure
        u"svg"_s, u"g"_s, u"defs"_s, u"symbol"_s, u"use"_s, u"switch"_s, u"a"_s,
        u"title"_s, u"desc"_s, u"style"_s,
        // shapes
        u"path"_s, u"rect"_s, u"circle"_s, u"ellipse"_s, u"line"_s, u"polyline"_s, u"polygon"_s,
        // text
        u"text"_s, u"tspan"_s, u"textPath"_s, u"textArea"_s, u"tbreak"_s,
        // paint servers, clipping, masking, markers
        u"linearGradient"_s, u"radialGradient"_s, u"stop"_s, u"solidColor"_s,
        u"clipPath"_s, u"mask"_s, u"pattern"_s, u"marker"_s,
        // raster content (data: URIs only, checked below)
        u"image"_s,
        // filters supported by Qt SVG
        u"filter"_s, u"feGaussianBlur"_s, u"feOffset"_s, u"feColorMatrix"_s, u"feMerge"_s,
        u"feMergeNode"_s, u"feComposite"_s, u"feFlood"_s, u"feBlend"_s,
    };
    return set;
}

const QSet<QString> &animationElements()
{
    static const QSet<QString> set = {
        u"animate"_s, u"animateTransform"_s, u"animateMotion"_s, u"animateColor"_s, u"set"_s, u"mpath"_s,
    };
    return set;
}

const QSet<QString> &allowedAttributes()
{
    static const QSet<QString> set = {
        // core and structure
        u"id"_s, u"class"_s, u"style"_s, u"transform"_s, u"viewBox"_s, u"preserveAspectRatio"_s,
        u"width"_s, u"height"_s, u"x"_s, u"y"_s, u"version"_s, u"baseProfile"_s, u"zoomAndPan"_s,
        u"href"_s, u"role"_s, u"focusable"_s, u"enable-background"_s, u"overflow"_s,
        u"requiredFeatures"_s, u"requiredExtensions"_s, u"systemLanguage"_s, u"type"_s, u"media"_s,
        // geometry
        u"x1"_s, u"y1"_s, u"x2"_s, u"y2"_s, u"cx"_s, u"cy"_s, u"r"_s, u"rx"_s, u"ry"_s,
        u"d"_s, u"points"_s, u"pathLength"_s,
        // presentation
        u"fill"_s, u"fill-opacity"_s, u"fill-rule"_s, u"stroke"_s, u"stroke-width"_s,
        u"stroke-opacity"_s, u"stroke-linecap"_s, u"stroke-linejoin"_s, u"stroke-miterlimit"_s,
        u"stroke-dasharray"_s, u"stroke-dashoffset"_s, u"opacity"_s, u"color"_s, u"display"_s,
        u"visibility"_s, u"paint-order"_s, u"vector-effect"_s, u"mix-blend-mode"_s, u"isolation"_s,
        u"color-interpolation"_s, u"color-interpolation-filters"_s, u"color-rendering"_s,
        u"shape-rendering"_s, u"text-rendering"_s, u"image-rendering"_s, u"solid-color"_s,
        u"solid-opacity"_s,
        // text
        u"font-family"_s, u"font-size"_s, u"font-weight"_s, u"font-style"_s, u"font-variant"_s,
        u"font-stretch"_s, u"text-anchor"_s, u"text-decoration"_s, u"letter-spacing"_s,
        u"word-spacing"_s, u"dx"_s, u"dy"_s, u"rotate"_s, u"textLength"_s, u"lengthAdjust"_s,
        u"dominant-baseline"_s, u"alignment-baseline"_s, u"baseline-shift"_s, u"writing-mode"_s,
        u"direction"_s, u"unicode-bidi"_s, u"startOffset"_s, u"method"_s, u"spacing"_s,
        // gradients
        u"offset"_s, u"stop-color"_s, u"stop-opacity"_s, u"gradientUnits"_s,
        u"gradientTransform"_s, u"spreadMethod"_s, u"fx"_s, u"fy"_s, u"fr"_s,
        // clipping, masking, patterns, markers
        u"clip-path"_s, u"clip-rule"_s, u"clipPathUnits"_s, u"mask"_s, u"maskUnits"_s,
        u"maskContentUnits"_s, u"patternUnits"_s, u"patternContentUnits"_s, u"patternTransform"_s,
        u"markerWidth"_s, u"markerHeight"_s, u"markerUnits"_s, u"refX"_s, u"refY"_s, u"orient"_s,
        u"marker-start"_s, u"marker-mid"_s, u"marker-end"_s, u"marker"_s,
        // filters
        u"filter"_s, u"filterUnits"_s, u"primitiveUnits"_s, u"in"_s, u"in2"_s, u"result"_s,
        u"stdDeviation"_s, u"values"_s, u"operator"_s, u"k1"_s, u"k2"_s, u"k3"_s, u"k4"_s,
        u"mode"_s, u"flood-color"_s, u"flood-opacity"_s, u"edgeMode"_s,
        // animation (rendered statically)
        u"attributeName"_s, u"attributeType"_s, u"begin"_s, u"dur"_s, u"end"_s, u"repeatCount"_s,
        u"repeatDur"_s, u"from"_s, u"to"_s, u"by"_s, u"keyTimes"_s, u"keySplines"_s,
        u"calcMode"_s, u"additive"_s, u"accumulate"_s, u"keyPoints"_s, u"path"_s, u"restart"_s,
        u"min"_s, u"max"_s,
    };
    return set;
}

bool isIgnoredNamespace(QStringView ns)
{
    static const QStringList ignored = {
        u"http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd"_s,
        u"http://www.inkscape.org/namespaces/inkscape"_s,
        u"http://www.w3.org/1999/02/22-rdf-syntax-ns#"_s,
        u"http://creativecommons.org/ns#"_s,
        u"http://web.resource.org/cc/"_s,
        u"http://purl.org/dc/elements/1.1/"_s,
    };
    return ignored.contains(ns) || ns.startsWith(u"http://ns.adobe.com/");
}

QString fail(const QString &message)
{
    return message;
}

// Checks every CSS url(...) in a value: only same-document fragments allowed.
QString checkCssReferences(QStringView value, const QString &context)
{
    if (value.contains(u'\\'))
        return fail(u"CSS escapes are not allowed (%1)"_s.arg(context));
    if (value.contains(u"@import", Qt::CaseInsensitive))
        return fail(u"CSS @import is not allowed (%1)"_s.arg(context));
    qsizetype pos = 0;
    while ((pos = value.indexOf(u"url(", pos, Qt::CaseInsensitive)) >= 0) {
        const qsizetype end = value.indexOf(u')', pos);
        if (end < 0)
            return fail(u"malformed url() (%1)"_s.arg(context));
        QStringView target = value.sliced(pos + 4, end - pos - 4).trimmed();
        if (target.size() >= 2 && (target.front() == u'"' || target.front() == u'\'')
            && target.back() == target.front())
            target = target.sliced(1, target.size() - 2).trimmed();
        if (!target.startsWith(u'#'))
            return fail(u"external reference url(%1) is not allowed (%2)"_s.arg(target.toString(), context));
        pos = end + 1;
    }
    return {};
}

QString checkDataUri(QStringView uri, const Limits &limits)
{
    static const QStringList prefixes = {
        u"data:image/png;base64,"_s, u"data:image/jpeg;base64,"_s, u"data:image/jpg;base64,"_s,
    };
    QStringView payload;
    for (const QString &prefix : prefixes) {
        if (uri.startsWith(prefix, Qt::CaseInsensitive)) {
            payload = uri.sliced(prefix.size());
            break;
        }
    }
    if (payload.isNull())
        return fail(u"only PNG and JPEG data: URIs are allowed"_s);
    if (payload.size() / 4 * 3 > limits.maxAssetBytes)
        return fail(u"embedded image exceeds the size limit"_s);

    QByteArray base64 = payload.toLatin1();
    base64.removeIf([](char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; });
    const auto decoded = QByteArray::fromBase64Encoding(base64, QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded)
        return fail(u"embedded image is not valid base64"_s);
    const auto format = detectFormat(*decoded);
    if (format != ImageFormat::Png && format != ImageFormat::Jpeg)
        return fail(u"embedded image is not a PNG or JPEG"_s);

    QBuffer buffer;
    buffer.setData(*decoded);
    buffer.open(QIODevice::ReadOnly);
    const QSize size = QImageReader(&buffer, qtFormatName(*format)).size();
    if (!size.isValid() || size.isEmpty() || qint64(size.width()) * size.height() > limits.maxDecodedPixels)
        return fail(u"embedded image has invalid or excessive dimensions"_s);
    return {};
}

QString checkReference(QStringView value, const Limits &limits)
{
    const QStringView v = value.trimmed();
    if (v.startsWith(u'#'))
        return {};
    if (v.startsWith(u"data:", Qt::CaseInsensitive))
        return checkDataUri(v, limits);
    return fail(u"external reference \"%1\" is not allowed"_s.arg(v.left(80).toString()));
}

} // namespace

SvgValidation validateSvg(const QByteArray &bytes, const Limits &limits)
{
    SvgValidation result;
    if (bytes.size() > limits.maxAssetBytes) {
        result.error = u"SVG exceeds the size limit"_s;
        return result;
    }

    QXmlStreamReader xml(bytes);
    int depth = 0;
    int skipDepth = 0; // > 0 while inside an ignored subtree
    int elements = 0;
    bool sawRoot = false;
    bool inStyle = false;

    auto reject = [&result](const QString &message) {
        result.ok = false;
        result.error = message;
        return result;
    };

    while (!xml.atEnd()) {
        switch (xml.readNext()) {
        case QXmlStreamReader::DTD:
            return reject(u"DOCTYPE declarations are not allowed"_s);
        case QXmlStreamReader::EntityReference:
            return reject(u"entity references are not allowed"_s);
        case QXmlStreamReader::ProcessingInstruction:
            return reject(u"processing instructions (e.g. xml-stylesheet) are not allowed"_s);
        case QXmlStreamReader::StartElement: {
            ++depth;
            if (depth > MaxDepth)
                return reject(u"SVG nesting is too deep"_s);
            if (++elements > MaxElements)
                return reject(u"SVG has too many elements"_s);
            if (skipDepth > 0) {
                ++skipDepth;
                break;
            }
            const QString ns = xml.namespaceUri().toString();
            const QString name = xml.name().toString();
            if (!sawRoot) {
                if (ns != SvgNs || name != u"svg"_s)
                    return reject(u"the root element must be <svg> in the SVG namespace"_s);
                sawRoot = true;
            }
            if (isIgnoredNamespace(ns) || (ns == SvgNs && name == u"metadata"_s)) {
                skipDepth = 1;
                break;
            }
            if (ns != SvgNs)
                return reject(u"unsupported element <%1>"_s.arg(xml.qualifiedName().toString()));
            if (name == u"script"_s || name == u"foreignObject"_s)
                return reject(u"<%1> is not allowed"_s.arg(name));
            if (animationElements().contains(name))
                result.animated = true;
            else if (!allowedElements().contains(name))
                return reject(u"unsupported element <%1>"_s.arg(name));
            inStyle = name == u"style"_s;

            const auto attributes = xml.attributes();
            for (const QXmlStreamAttribute &attribute : attributes) {
                const QString attrNs = attribute.namespaceUri().toString();
                const QString attrName = attribute.name().toString();
                const QStringView value = attribute.value();
                if (attrName.startsWith(u"on"_s, Qt::CaseInsensitive) && attrNs.isEmpty())
                    return reject(u"event attribute \"%1\" is not allowed"_s.arg(attrName));
                if (attrNs == XlinkNs || (attrNs.isEmpty() && attrName == u"href"_s)) {
                    if (attrName == u"href"_s) {
                        if (const QString e = checkReference(value, limits); !e.isEmpty())
                            return reject(e);
                    }
                    continue;
                }
                if (!attrNs.isEmpty())
                    continue; // xml:space, editor and other foreign attributes have no effect
                if (!allowedAttributes().contains(attrName) && !attrName.startsWith(u"data-"_s)
                    && !attrName.startsWith(u"aria-"_s))
                    return reject(u"unsupported attribute \"%1\" on <%2>"_s.arg(attrName, name));
                if (const QString e = checkCssReferences(value, u"attribute \"%1\""_s.arg(attrName)); !e.isEmpty())
                    return reject(e);
            }
            break;
        }
        case QXmlStreamReader::EndElement:
            --depth;
            if (skipDepth > 0)
                --skipDepth;
            inStyle = false;
            break;
        case QXmlStreamReader::Characters:
            if (inStyle && skipDepth == 0) {
                if (const QString e = checkCssReferences(xml.text(), u"<style>"_s); !e.isEmpty())
                    return reject(e);
            }
            break;
        default:
            break;
        }
    }

    if (xml.hasError())
        return reject(u"malformed SVG: %1"_s.arg(xml.errorString()));
    if (!sawRoot)
        return reject(u"no <svg> element found"_s);
    result.ok = true;
    return result;
}

} // namespace glacier::io
