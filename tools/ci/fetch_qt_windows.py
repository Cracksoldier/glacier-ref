#!/usr/bin/env python3
"""Fetch prebuilt Qt for Windows/MSVC from the official Qt online repository.

Used by CI because aqtinstall 3.3.0 does not understand the per-architecture
Windows repository layout used by current Qt releases (see docs/TOOLCHAIN.md).
Standard library only. Downloads only the requested components, verifies each
archive against the SHA-1 published on download.qt.io (fetched from the
official host, never from a mirror), and extracts with 7-Zip.

Example:
    python fetch_qt_windows.py --version 6.12.0 --arch win64_msvc2022_64 \
        --components qtbase,qtsvg,qtimageformats --output C:/Qt/6.12.0/msvc2022_64

In GitHub Actions it also exports QT_ROOT_DIR and CMAKE_PREFIX_PATH and adds
the Qt bin directory to PATH.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path

OFFICIAL_HOST = "download.qt.io"
REPOSITORY = f"https://{OFFICIAL_HOST}/online/qtsdkrepository/windows_x86/desktop"
SAFE_NAME = re.compile(r"^[A-Za-z0-9._+-]+$")
MAX_METADATA_BYTES = 8 * 1024 * 1024
USER_AGENT = "glacier-ref-ci-qt-fetch"


def fail(message: str) -> None:
    print(f"fetch_qt_windows: error: {message}", file=sys.stderr)
    sys.exit(1)


def open_url(url: str):
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    try:
        return urllib.request.urlopen(request, timeout=120)
    except urllib.error.HTTPError as error:
        fail(f"HTTP {error.code} for {url} (wrong Qt version or architecture?)")
    except urllib.error.URLError as error:
        fail(f"cannot reach {url}: {error.reason}")


def fetch_official(url: str) -> bytes:
    """Fetch small metadata; refuse anything not served by the official host."""
    with open_url(url) as response:
        final_host = urllib.parse.urlparse(response.geturl()).hostname
        if final_host != OFFICIAL_HOST:
            fail(f"{url} was redirected to {final_host}; metadata must come from {OFFICIAL_HOST}")
        data = response.read(MAX_METADATA_BYTES + 1)
    if len(data) > MAX_METADATA_BYTES:
        fail(f"{url} is unexpectedly large")
    return data


def download_verified(url: str, expected_sha1: str, target: Path) -> None:
    """Download (mirrors allowed) and verify against the official SHA-1."""
    digest = hashlib.sha1()
    with open_url(url) as response, target.open("wb") as out:
        while chunk := response.read(1 << 20):
            digest.update(chunk)
            out.write(chunk)
    if digest.hexdigest() != expected_sha1:
        fail(f"checksum mismatch for {target.name}: got {digest.hexdigest()}, expected {expected_sha1}")


def arch_folder(arch: str) -> str:
    # win64_msvc2022_64 -> msvc2022_64
    if not arch.startswith("win64_"):
        fail(f"unsupported architecture {arch!r}; expected win64_*")
    return arch.removeprefix("win64_")


def select_archives(updates: ET.Element, arch: str, components: list[str]) -> list[tuple[str, str, str]]:
    """Return (package name, version, archive file) for each requested component."""
    candidates = []
    for package in updates.iter("PackageUpdate"):
        name = package.findtext("Name", "")
        if not name.endswith(f".{arch}") or ".debug_info" in name:  # debug_info, debug_information
            continue
        archives = [a.strip() for a in package.findtext("DownloadableArchives", "").split(",") if a.strip()]
        candidates.append((name, package.findtext("Version", ""), archives))

    selected = []
    for component in components:
        matches = [
            (name, version, archive)
            for name, version, archives in candidates
            for archive in archives
            if archive.startswith(f"{component}-") and archive.endswith(".7z")
        ]
        if len(matches) != 1:
            fail(f"expected exactly one archive for {component!r}, found {len(matches)}")
        for value in matches[0]:
            if not SAFE_NAME.match(value):
                fail(f"unexpected characters in repository metadata: {value!r}")
        selected.append(matches[0])
    return selected


def export_github_env(prefix: Path) -> None:
    env_file = os.environ.get("GITHUB_ENV")
    path_file = os.environ.get("GITHUB_PATH")
    if env_file:
        with open(env_file, "a", encoding="utf-8") as f:
            f.write(f"QT_ROOT_DIR={prefix}\n")
            f.write(f"CMAKE_PREFIX_PATH={prefix}\n")
    if path_file:
        with open(path_file, "a", encoding="utf-8") as f:
            f.write(f"{prefix / 'bin'}\n")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--version", required=True, help="Qt version, e.g. 6.12.0")
    parser.add_argument("--arch", required=True, help="Qt package architecture, e.g. win64_msvc2022_64")
    parser.add_argument("--components", default="qtbase,qtsvg,qtimageformats",
                        help="comma-separated archive components (default: %(default)s)")
    parser.add_argument("--output", required=True, type=Path, help="Qt prefix to extract into")
    parser.add_argument("--dry-run", action="store_true", help="only list the archives that would be fetched")
    args = parser.parse_args()

    if not re.fullmatch(r"6\.\d+\.\d+", args.version):
        fail(f"unsupported Qt version {args.version!r}")
    nodot = args.version.replace(".", "")
    repo = f"{REPOSITORY}/qt6_{nodot}/qt6_{nodot}_{arch_folder(args.arch)}"
    updates = ET.fromstring(fetch_official(f"{repo}/Updates.xml"))
    components = [c.strip() for c in args.components.split(",") if c.strip()]
    archives = select_archives(updates, args.arch, components)
    if args.dry_run:
        for package, version, archive in archives:
            print(f"{repo}/{package}/{version}{archive}")
        return

    sevenzip = shutil.which("7z")
    if not sevenzip:
        fail("7-Zip (7z) is required on PATH")

    prefix = args.output.resolve()
    prefix.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="qt-fetch-") as tmp:
        for package, version, archive in archives:
            url = f"{repo}/{package}/{version}{archive}"
            expected = fetch_official(f"{url}.sha1").decode("ascii").split()[0].lower()
            if not re.fullmatch(r"[0-9a-f]{40}", expected):
                fail(f"invalid SHA-1 file for {archive}")
            target = Path(tmp) / archive
            print(f"fetch_qt_windows: {archive} (sha1 {expected})", flush=True)
            download_verified(url, expected, target)
            subprocess.run([sevenzip, "x", "-y", "-bso0", "-bsp0", f"-o{prefix}", str(target)], check=True)
            target.unlink()

    if not (prefix / "lib" / "cmake" / "Qt6" / "Qt6Config.cmake").is_file() and "qtbase" in components:
        fail("Qt6Config.cmake not found after extraction")
    print(f"fetch_qt_windows: Qt {args.version} ({args.arch}) installed to {prefix}")
    export_github_env(prefix)


if __name__ == "__main__":
    main()
