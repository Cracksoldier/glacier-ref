#pragma once

#include <QtGlobal>

namespace glacier {

// Safety limits from spec §4 ("initial configurable safeguards"). They protect
// against hostile or accidental input; they are not performance guarantees.
// Tests inject smaller values.
struct Limits
{
    qint64 maxAssetBytes = qint64(100) * 1024 * 1024; // per input asset
    qint64 maxDecodedPixels = qint64(100) * 1000 * 1000; // 100 megapixels
    qint64 maxBoardFileBytes = qint64(2) * 1024 * 1024 * 1024;
    int maxItems = 10000;
    int maxNoteCharacters = 100000;

    // Board files (spec §5, M3.04): bounds checked before allocation.
    int maxArchiveEntries = 25000;
    qint64 maxManifestBytes = qint64(64) * 1024 * 1024;
    qint64 maxUncompressedBytes = qint64(4) * 1024 * 1024 * 1024;
    int maxCompressionRatio = 200;                       // for entries above ratioCheckBytes
    qint64 ratioCheckBytes = qint64(1) * 1024 * 1024;
    int maxJsonDepth = 32;

    // Spec §3: supported coordinate range is +/- 10 million logical units.
    static constexpr double CoordinateLimit = 10'000'000.0;
};

} // namespace glacier
