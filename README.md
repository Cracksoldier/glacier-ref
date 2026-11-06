# Glacier Ref

Glacier Ref is an offline desktop reference board for artists, 3D modelers,
developers, and designers: collect images and notes on an infinite canvas and
keep them beside your creative tools. It is written in C++20 with Qt 6 Widgets.

Glacier Ref is an independent project inspired by PureRef's minimal canvas
workflow. It is not affiliated with PureRef and does not read or write `.pur`
files.

- **Offline only:** no accounts, cloud, telemetry, update checks, or URL downloads.
- **Platforms:** Linux (native Wayland first) and Windows; macOS is secondary.
- **License:** MIT for Glacier Ref's own source; bundled components keep their
  own licenses (see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).

## Status

Early development: milestone **M0 (foundation)**. The application currently
opens an empty canvas window. See the
[milestones](docs/GLACIER_REF_MILESTONES.md) and the
[specification](docs/GLACIER_REF_SPECIFICATION.md).

## Building

Requirements: CMake 3.21+, Ninja, a C++20 compiler, and Qt 6.12+ with the
Svg and Image Formats modules.

```sh
cmake --preset dev-release
cmake --build --preset dev-release
ctest --preset dev-release
./build/dev-release/app/glacier-ref
```

Per-platform instructions are in [docs/BUILDING.md](docs/BUILDING.md).

## Documentation

- [Building](docs/BUILDING.md) · [Toolchain pins](docs/TOOLCHAIN.md) · [Dependencies](docs/DEPENDENCIES.md)
- [Architecture](docs/ARCHITECTURE.md) · [Offline audit](docs/OFFLINE_AUDIT.md)
- [Contributing](CONTRIBUTING.md)
