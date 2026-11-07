# The `.glacierref` board format, schema version 1

Milestone M3.01. This document is the contract between Glacier Ref versions:
anything that writes or reads boards follows it. The implementation lives in
`persistence/` (`Manifest`, `BoardSerializer`); fixtures in
`tests/fixtures/boards/` pin it down.

| Property | Value |
| --- | --- |
| File extension | `.glacierref` |
| MIME type | `application/x-glacierref` |
| Container | ZIP (PKWARE APPNOTE; ZIP64 where needed), written by miniz |
| Format identifier | `glacier-ref` |
| Current schema version | `1` |

## Archive entries

| Entry | Required | Compression | Content |
| --- | --- | --- | --- |
| `manifest.json` | yes | deflate | UTF-8 JSON document, described below |
| `assets/<sha256>.<ext>` | for each embedded asset | stored (PNG, JPEG, WebP, GIF) or deflate (BMP, TIFF, SVG) | the original asset bytes, unchanged |
| `preview.png` | no | stored | thumbnail of the board, longest side at most 512 px |

- `<sha256>` is the 64-character lowercase hex SHA-256 of the entry's bytes.
- `<ext>` is `png`, `jpg`, `webp`, `bmp`, `gif`, `tiff` or `svg`, from the asset's media type.
- Readers find entries by exact name. Entry names never start with `/`, and never contain `\`, `..` segments, empty segments or drive letters; an archive with such a name, or with two entries of the same name, is rejected.
- Readers ignore entries they don't know, and the preview is never required to load a board.
- Writers must not add entries outside this table, except under a `x-<vendor>/` prefix.

## Manifest

A JSON object. Readers ignore unknown members, so later minor additions don't need a new schema version. Anything whose meaning a reader can't ignore safely requires a new schema version.

```json
{
  "format": "glacier-ref",
  "schemaVersion": 1,
  "documentId": "3f2b8c1e-7a4d-4e0f-9b6a-2c5d8e1f0a7b",
  "title": "Character sheet",
  "created": "2026-11-07T10:00:00.000Z",
  "modified": "2026-11-07T11:30:12.345Z",
  "generator": "Glacier Ref 0.1.0",
  "assets": [],
  "items": [],
  "groups": [],
  "view": {}
}
```

| Member | Type | Meaning |
| --- | --- | --- |
| `format` | string | always `glacier-ref` |
| `schemaVersion` | integer | `1` |
| `documentId` | ID | identifies the board across saves and copies; Save As keeps it |
| `title` | string, ≤ 256 characters, no control characters | user-editable board title; defaults to the file base name at first save |
| `created`, `modified` | timestamp | creation time and time of this save |
| `generator` | string, optional | application and version that wrote the file; informational |
| `assets` | array of assets | images referenced by items |
| `items` | array of items | images and notes |
| `groups` | array of groups | flat groups |
| `view` | object | board view settings |

### Common value types

- **ID:** a UUID as a 36-character lowercase string with hyphens and no braces, for example `3f2b8c1e-7a4d-4e0f-9b6a-2c5d8e1f0a7b`. Other spellings and the nil UUID are rejected. IDs are unique across all assets, items and groups of a board.
- **Timestamp:** UTC, ISO 8601 with milliseconds and a `Z` suffix, for example `2026-11-07T10:00:00.000Z`.
- **Number:** a JSON number. Numbers must be finite; there are no `NaN` or infinity values. Writers use the shortest round-trip representation, independent of locale.
- **Colour:** `#AARRGGBB`, 8 hex digits (upper or lower case), alpha first.
- **Transform:** an array of six numbers `[m11, m12, m21, m22, dx, dy]` (see [Coordinates](#coordinates-and-transforms)).

### Assets

Every asset has these members:

| Member | Type | Meaning |
| --- | --- | --- |
| `id` | ID | referenced by image items |
| `kind` | `"embedded"` or `"linked"` | where the bytes are |
| `sha256` | string | lowercase hex SHA-256 of the bytes; unique per board |
| `mediaType` | string | `image/png`, `image/jpeg`, `image/webp`, `image/bmp`, `image/gif`, `image/tiff` or `image/svg+xml` |
| `width`, `height` | integers > 0 | oriented pixel size: after EXIF orientation; for SVG, the intrinsic size |
| `byteLength` | integer > 0 | length of the bytes |

**Embedded** assets also have `entry`, the archive entry name `assets/<sha256>.<ext>`. Readers verify that the entry exists, that its length equals `byteLength` and that its SHA-256 equals `sha256`, and they check that the content's detected format and dimensions match `mediaType`, `width` and `height`.

**Linked** assets also have:

| Member | Type | Meaning |
| --- | --- | --- |
| `path` | string | location of the file, with `/` separators |
| `pathKind` | `"relative"` or `"absolute"` | relative paths are resolved against the directory containing the board file |

For linked assets, `sha256`, `byteLength`, `width` and `height` describe the file **as last accepted**. A reader that finds the file with a different digest reports it as changed and doesn't display the new content until the user accepts it. A missing file is shown as a placeholder of the item's size.

Writers store a relative path when the board and the target share an ancestor directory below the filesystem root, and on Windows the same drive. Otherwise they store an absolute path, which makes the board less portable. Absolute Windows paths are written as `C:/Users/…`, and UNC paths as `//server/share/…`.

### Items

Each item has `id`, `type`, `transform`, `z` (number), `locked` (boolean) and, for group members, `group` (the group's ID). Other members depend on `type`:

**`"image"`:**

| Member | Type | Meaning |
| --- | --- | --- |
| `asset` | ID | an asset of this board |
| `size` | `[width, height]`, numbers > 0 | display size in item-local units; normally the asset's oriented size |
| `crop` | `[x, y, width, height]` | normalized crop rectangle in image-local coordinates; `[0, 0, 1, 1]` is uncropped; `x, y ≥ 0`, `width, height > 0`, `x + width ≤ 1`, `y + height ≤ 1` |

**`"note"`:**

| Member | Type | Meaning |
| --- | --- | --- |
| `text` | string, ≤ 100,000 characters | plain Unicode text; `\n` separates paragraphs |
| `width` | number > 0 | wrap width in item-local units |
| `height` | number > 0 | height of the laid-out text including padding, as computed by the writer; readers may recompute it |
| `fontSize` | number, 1–1000 | point size |
| `textColor`, `backgroundColor` | colour | note colours |

### Groups

| Member | Type | Meaning |
| --- | --- | --- |
| `id` | ID | |
| `name` | string, ≤ 256 characters | may be empty |
| `transform` | transform | maps group-local coordinates to the board |
| `children` | array of IDs, non-empty | member items in stacking order, back to front |
| `z` | number | stacking position of the whole group among top-level entities |
| `locked` | boolean | a locked group protects its members |

Groups are flat: members are image or note items, never groups. Every item listed in a group's `children` has that group's ID in its `group` member and vice versa, and an item belongs to at most one group.

### View

| Member | Type | Meaning |
| --- | --- | --- |
| `background` | colour | board background, opaque |
| `grayscale` | boolean | grayscale preview of images |
| `center` | `[x, y]` | board point at the centre of the window when saved |
| `zoom` | number, 0.01–64 | zoom factor when saved |

Window geometry is a local preference and is not stored.

## Coordinates and transforms

Board coordinates are logical units with the y axis pointing down. The supported range is ±10,000,000 units.

A transform `[m11, m12, m21, m22, dx, dy]` maps a point `(x, y)` to

```text
x' = m11·x + m21·y + dx
y' = m12·x + m22·y + dy
```

This is Qt's `QTransform` convention with row vectors: "first A, then B" is written `A·B`. The determinant `m11·m22 − m12·m21` must be non-zero. Positive rotation angles turn clockwise on screen.

An image pixel reaches the screen through these steps, in order:

1. **Source orientation:** the decoded image is turned upright using its EXIF orientation. `width` and `height` of the asset are the size after this step.
2. **Crop:** the normalized `crop` rectangle selects the visible part of the upright image. Cropping doesn't move the picture: a point of the picture keeps its item-local position.
3. **Local scaling:** the upright image covers item-local `(0, 0)–(size)`. The visible part is the crop rectangle scaled by `size`.
4. **Item transform:** maps item-local coordinates to the parent: the board for top-level items, or the group's local frame for group members.
5. **Group transform:** maps group-local coordinates to the board.
6. **Scene:** the view's centre and zoom map board coordinates to the window.

So the board transform of a group member is `item.transform · group.transform`. Flips are part of the transform (negative determinant). Notes use the same item and group transforms with the local rectangle `(0, 0)–(width, height)`.

Within a parent, entities are drawn in increasing `z`. Members of a group are drawn in `children` order, all at the group's position in the stack.

## Validation and limits

A reader validates the whole manifest and every required asset before showing the board, and rejects a board that breaks any rule in this document. In addition, these default limits apply before allocation:

| Limit | Default |
| --- | --- |
| Board file size | 2 GiB |
| Archive entries | 25,000 |
| Total uncompressed size | 4 GiB |
| Compression ratio of an entry above 1 MiB | 200:1 |
| `manifest.json` size | 64 MiB |
| JSON nesting depth | 32 |
| Items (images and notes) | 10,000 |
| Note length | 100,000 characters |
| Asset size | 100 MiB |
| Decoded image size | 100 megapixels |

## Compatibility policy

- **Same schema version:** readers accept files with unknown extra members and ignore them.
- **Older schema version:** readers upgrade the manifest in memory through one explicit migration function per version step (`1 → 2`, `2 → 3`, …), each covered by fixture tests. The upgraded board is written in the current schema only when the user saves it.
- **Newer schema version:** readers refuse the file with "This board was made by a newer version of Glacier Ref" and never rewrite it.
- **Unknown `format`:** the file is not a Glacier Ref board.
- Glacier Ref does not read or write PureRef `.pur` files.

## Related files

- **Document lock:** while a board is open for editing, Glacier Ref keeps a hidden lock file next to it, `.<board file name>.lock`. It may appear in synced folders, and is removed when the board is closed. A board whose lock is held by another process opens read-only.
- **Recovery data** lives in the application data directory, never next to the board. Its snapshot manifests use this schema, with assets referenced in a shared recovery pool instead of archive entries.
