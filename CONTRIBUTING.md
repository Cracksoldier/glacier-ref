# Contributing to Glacier Ref

Thanks for your interest. The [specification](docs/GLACIER_REF_SPECIFICATION.md)
is authoritative for behavior and architecture; the
[milestones](docs/GLACIER_REF_MILESTONES.md) define the current work.

## Ground rules

- **Offline only.** No networking code, Qt Network, telemetry, update checks, or
  URL fetching. Run the [offline audit](docs/OFFLINE_AUDIT.md) when touching
  startup, import, rendering, or help.
- **Public Qt APIs only**, Qt Widgets/QGraphicsView, CMake. Changing these
  requires a specification revision.
- **No locale-dependent serialization.** Use `QString::number`, `QJsonDocument`,
  and similar locale-independent APIs for anything written to disk.
- **User-visible strings go through `tr()`.** The v1 UI is English only, but
  translations must be possible later.
- **Ownership:** RAII, QObject parents, and smart pointers; no raw owning
  pointers (see [Architecture](docs/ARCHITECTURE.md)).
- Original assets and branding only.

## Building and testing

See [docs/BUILDING.md](docs/BUILDING.md). Before sending a change, run:

```sh
cmake --preset asan && cmake --build --preset asan && ctest --preset asan
cmake --preset ci-release && cmake --build --preset ci-release && ctest --preset ci-release
```

Every test runs twice: in the default locale and as `<name>.de_AT` with a
comma decimal separator. On Linux, the `de_AT.UTF-8` locale must be generated
(`/etc/locale.gen`, then `locale-gen`).

CI builds with warnings as errors on GCC, MSVC, and Apple Clang.

For clangd, link the compile database once: `ln -s build/dev-debug/compile_commands.json .`

## Code style

- C++20, namespace `glacier`, one class per `PascalCase.h/.cpp` pair.
- Qt string literals via `using namespace Qt::StringLiterals;` and `u"..."_s`
  (`QT_NO_CAST_FROM_ASCII` is enabled).
- Keep the model (`core/`) free of graphics items and GUI dependencies.

## Commits

Reference milestone task IDs, e.g. `M1.04: accept local file: URLs on drop`.
Mark a milestone task complete only when its behavior has been verified.
