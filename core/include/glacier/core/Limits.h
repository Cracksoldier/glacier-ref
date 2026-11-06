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

    // Spec §3: supported coordinate range is +/- 10 million logical units.
    static constexpr double CoordinateLimit = 10'000'000.0;
};

} // namespace glacier
