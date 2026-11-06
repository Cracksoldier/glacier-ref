#pragma once

#include "glacier/core/BoardDocument.h"
#include "glacier/core/Limits.h"

#include <QByteArray>
#include <QHash>

#include <optional>

namespace glacier::editing {

// Board items on the clipboard (spec §3): the records of the copied entities
// plus the bytes of their embedded assets, so pasting works in other windows
// and other Glacier Ref processes. Encoded as CBOR under a private MIME type.
struct ClipboardFragment
{
    Snapshot snapshot;
    QHash<QByteArray, QByteArray> assetBytes; // sha256 -> bytes

    static constexpr qint64 MaxTotalBytes = qint64(1024) * 1024 * 1024;
};

inline constexpr char FragmentMimeType[] = "application/x-glacierref-items";

QByteArray encodeFragment(const ClipboardFragment &fragment);

// Decodes untrusted clipboard data: checks structure, types, finite numbers,
// canonical IDs, sizes against `limits`, and that every asset's bytes match
// its SHA-256. Document-level consistency is checked again when pasting.
std::optional<ClipboardFragment> decodeFragment(const QByteArray &data, const Limits &limits = {},
                                                QString *error = nullptr);

} // namespace glacier::editing
