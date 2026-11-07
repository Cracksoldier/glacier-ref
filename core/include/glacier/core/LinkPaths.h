#pragma once

#include <QString>

namespace glacier::link_paths {

// Paths of linked assets (spec §4, docs/FORMAT.md). They use '/' separators
// on every platform and are interpreted with the same syntax everywhere, so a
// board made on Windows keeps its links readable (as missing) on Linux:
//
//   "/home/a/x.png"            Unix root "/"
//   "C:/Users/a/x.png"         drive root "C:/" (letter compared case-insensitively)
//   "//server/share/x.png"     UNC root "//server/share/"
//
// Everything else is relative.

// The root of an absolute path ("/", "C:/", "//server/share/"), or an empty
// string for a relative path.
QString root(const QString &path);
bool isAbsolute(const QString &path);

// Converts native separators to '/' and removes ".", empty segments and
// resolvable "..": the same result on every platform, unlike QDir::cleanPath.
// ".." above an absolute root is dropped; leading ".." of a relative path stay.
QString normalize(const QString &path);

// A path as stored in a board manifest.
struct StoredPath
{
    QString path;
    bool relative = false;

    friend bool operator==(const StoredPath &, const StoredPath &) = default;
};

// Relative when the board directory and the target share an ancestor below
// their common root (on Windows also: the same drive); absolute otherwise, or
// when the board has no directory yet (unsaved). `absoluteTarget` and
// `boardDir` are absolute paths.
StoredPath toStored(const QString &absoluteTarget, const QString &boardDir);

// Resolves a stored path against the board directory. Returns an empty string
// for an invalid combination (empty path, relative path without a board
// directory, or a path whose kind doesn't match).
QString resolve(const StoredPath &stored, const QString &boardDir);

} // namespace glacier::link_paths
