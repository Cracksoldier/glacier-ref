#pragma once

#include "glacier/core/BoardDocument.h"
#include "glacier/core/Limits.h"

#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QPointF>
#include <QString>

#include <functional>
#include <optional>

namespace glacier::persistence {

// manifest.json of a .glacierref board (docs/FORMAT.md). Encoding and decoding
// are locale-independent; decoding is strict and validates the complete board
// before anything uses it (spec §5, M3.03).

inline constexpr int CurrentSchemaVersion = 1;
QString formatIdentifier(); // "glacier-ref"

// Everything a board file stores.
struct BoardContent
{
    BoardMetadata metadata;
    Snapshot snapshot;
    ViewSettings view;
    QPointF viewCenter;
    double zoom = 1.0;

    friend bool operator==(const BoardContent &, const BoardContent &) = default;
};

struct ManifestWriteOptions
{
    // Directory of the board file. Linked paths are written relative to it
    // where possible; empty (unsaved board, recovery): absolute.
    QString boardDir;
    QString generator;  // e.g. "Glacier Ref 0.1.0"
    QJsonObject extra;  // extra top-level members (e.g. recovery metadata)
};

// Serializes the board. Only assets referenced by an image are written;
// unreferenced ones (kept alive for undo) are dropped.
QByteArray writeManifest(const BoardContent &content, const ManifestWriteOptions &options);

// Archive entry for an embedded asset: "assets/<sha256>.<ext>".
QString assetEntryName(const QByteArray &sha256, const QString &mediaType);

// Schema migrations (compatibility policy): migrations[n] upgrades a manifest
// from version n to n + 1 in place. Version 1 has none yet.
using Migration = std::function<bool(QJsonObject &manifest, QString *error)>;
using MigrationTable = QMap<int, Migration>;
const MigrationTable &builtInMigrations();

// Upgrades `manifest` to `targetVersion`. Fails for newer versions and for
// older ones without a migration path.
bool migrateManifest(QJsonObject &manifest, const MigrationTable &migrations, int targetVersion, QString *error);

// Deepest nesting of arrays/objects in JSON text, found without parsing it
// (so excessive depth is rejected before QJsonDocument allocates).
int jsonNestingDepth(const QByteArray &json);

struct ManifestReadResult
{
    BoardContent content;
    QJsonObject root; // the manifest after migration, for extra members
};

// Parses, migrates and validates a manifest. `boardDir` resolves relative
// link paths (linked assets come back with absolute paths).
std::optional<ManifestReadResult> readManifest(const QByteArray &json, const QString &boardDir, const Limits &limits,
                                               QString *error, const MigrationTable &migrations = builtInMigrations(),
                                               int currentVersion = CurrentSchemaVersion);

} // namespace glacier::persistence
