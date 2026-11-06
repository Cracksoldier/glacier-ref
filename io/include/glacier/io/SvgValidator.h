#pragma once

#include "glacier/core/Limits.h"

#include <QByteArray>
#include <QString>

namespace glacier::io {

struct SvgValidation
{
    bool ok = false;
    QString error;      // names the offending construct when !ok
    bool animated = false; // contains animation elements (rendered statically)
};

// Validates an SVG document before Qt SVG sees it (spec §4).
//
// Accepted: self-contained vector content from an element whitelist, references
// to same-document fragments (#id), and bounded PNG/JPEG data: URIs. Editor
// metadata (metadata element, sodipodi/inkscape/rdf/cc/dc/Adobe namespaces) is
// ignored. Rejected: scripts, foreignObject, event attributes, DTDs and entity
// references, processing instructions (e.g. xml-stylesheet), CSS @import and
// escapes, any other reference (http:, file:, relative paths, other data:
// types), unknown elements and attributes, and documents beyond structural
// limits. The check never performs I/O.
SvgValidation validateSvg(const QByteArray &bytes, const Limits &limits = {});

} // namespace glacier::io
