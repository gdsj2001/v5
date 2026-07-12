#!/usr/bin/env python3
"""Verify the packaged and rootfs LinuxCNC runtime against the V5 allowlist."""

from __future__ import annotations

import argparse
import hashlib
import shutil
import subprocess
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_ALLOWLIST = (
    PROJECT_ROOT
    / "board"
    / "linuxcnc"
    / "yocto"
    / "files"
    / "v5_linuxcnc_runtime_allowlist.tsv"
)
GENERATED_RUNTIME_FILES = {
    "/usr/share/v5-native/linuxcnc-runtime-allowlist.tsv",
    "/usr/share/v5-native/linuxcnc-runtime-files.sha256",
    "/usr/share/v5-native/linuxcnc-source-identity.txt",
    "/usr/share/v5-native/v5_linuxcnc_source_identity.json",
}
FORBIDDEN_EXECUTABLES = {
    "axis",
    "gladevcp",
    "gmoccapy",
    "gscreen",
    "ngcgui",
    "pncconf",
    "pyngcgui",
    "pyvcp",
    "qtvcp",
    "stepconf",
    "tklinuxcnc",
    "touchy",
}
FORBIDDEN_NEEDED_PREFIXES = (
    "libQt",
    "libgdk",
    "libgtk",
    "libepoxy",
    "libtk",
    "libwayland",
    "libX11",
    "libXext",
    "libXinerama",
)
FORBIDDEN_PACKAGE_PREFIXES = (
    "gtk+",
    "gtk3",
    "libepoxy",
    "libgtk",
    "libx11",
    "libxcb",
    "libxext",
    "libxinerama",
    "libxmu",
    "packagegroup-core-x11",
    "packagegroup-petalinux-matchbox",
    "packagegroup-petalinux-qt",
    "python3-pyqt5",
    "qt",
    "tk",
    "wayland",
    "weston",
    "xserver-xorg",
)


def fail(message: str) -> None:
    raise RuntimeError(message)


def parse_allowlist(path: Path) -> dict[str, str]:
    rows: dict[str, str] = {}
    for line_number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not raw or raw.startswith("#"):
            continue
        fields = raw.split("\t")
        if len(fields) != 2:
            fail(f"allowlist line {line_number} must have path and consumer")
        runtime_path, consumer = fields
        if not runtime_path.startswith("/") or ".." in Path(runtime_path).parts:
            fail(f"unsafe allowlist path at line {line_number}: {runtime_path}")
        if not consumer.strip():
            fail(f"missing consumer at line {line_number}: {runtime_path}")
        if runtime_path in rows:
            fail(f"duplicate allowlist path: {runtime_path}")
        rows[runtime_path] = consumer
    if len(rows) != 32:
        fail(f"expected 32 runtime allowlist rows, got {len(rows)}")
    return rows


def rooted(root: Path, runtime_path: str) -> Path:
    return root / runtime_path.lstrip("/")


def enumerate_files(root: Path) -> set[str]:
    files: set[str] = set()
    for path in root.rglob("*"):
        if path.is_file() or path.is_symlink():
            files.add("/" + path.relative_to(root).as_posix())
    return files


def verify_runtime_hashes(package_root: Path, allowlist: dict[str, str]) -> None:
    hash_path = rooted(package_root, "/usr/share/v5-native/linuxcnc-runtime-files.sha256")
    seen: set[str] = set()
    for raw in hash_path.read_text(encoding="utf-8").splitlines():
        digest, relative = raw.split(maxsplit=1)
        relative = relative.lstrip("*")
        if relative.startswith("."):
            relative = relative[1:]
        runtime_path = relative if relative.startswith("/") else "/" + relative
        payload = rooted(package_root, runtime_path).read_bytes()
        actual = hashlib.sha256(payload).hexdigest()
        if actual != digest:
            fail(f"runtime hash mismatch: {runtime_path}")
        seen.add(runtime_path)
    if seen != set(allowlist):
        fail("runtime hash manifest does not match the source allowlist")


def verify_elf_dependencies(package_root: Path, runtime_files: set[str]) -> None:
    readelf = shutil.which("readelf")
    if not readelf:
        fail("readelf is required for the runtime dependency audit")
    for runtime_path in sorted(runtime_files):
        path = rooted(package_root, runtime_path)
        if path.is_symlink() or not path.is_file():
            continue
        result = subprocess.run(
            [readelf, "-d", str(path)],
            text=True,
            capture_output=True,
            check=False,
        )
        if result.returncode != 0:
            continue
        for line in result.stdout.splitlines():
            if "(NEEDED)" not in line:
                continue
            library = line.split("[", 1)[-1].split("]", 1)[0]
            if library.startswith(FORBIDDEN_NEEDED_PREFIXES):
                fail(f"forbidden GUI dependency {library} in {runtime_path}")


def verify_package_root(package_root: Path, allowlist_path: Path) -> tuple[int, int]:
    allowlist = parse_allowlist(allowlist_path)
    expected = set(allowlist) | GENERATED_RUNTIME_FILES
    actual = enumerate_files(package_root)
    missing = sorted(expected - actual)
    unexpected = sorted(actual - expected)
    if missing:
        fail(f"minimal package is missing files: {missing}")
    if unexpected:
        fail(f"minimal package contains non-allowlisted files: {unexpected}")

    installed_allowlist = rooted(
        package_root, "/usr/share/v5-native/linuxcnc-runtime-allowlist.tsv"
    )
    if installed_allowlist.read_bytes() != allowlist_path.read_bytes():
        fail("installed runtime allowlist differs from the Windows owner")
    verify_runtime_hashes(package_root, allowlist)
    verify_elf_dependencies(package_root, set(allowlist))

    total_bytes = sum(
        rooted(package_root, path).stat().st_size
        for path in actual
        if not rooted(package_root, path).is_symlink()
    )
    return len(actual), total_bytes


def package_names(manifest: Path) -> set[str]:
    names: set[str] = set()
    for raw in manifest.read_text(encoding="utf-8", errors="strict").splitlines():
        fields = raw.split()
        if fields:
            names.add(fields[0].lower())
    return names


def verify_rootfs(rootfs: Path, manifest: Path | None, allowlist: dict[str, str]) -> None:
    for runtime_path in allowlist:
        if not rooted(rootfs, runtime_path).exists():
            fail(f"rootfs is missing allowlisted runtime file: {runtime_path}")
    for executable in FORBIDDEN_EXECUTABLES:
        if rooted(rootfs, f"/usr/bin/{executable}").exists():
            fail(f"forbidden upstream GUI executable in rootfs: {executable}")
    forbidden_globs = (
        "usr/bin/wish*",
        "usr/lib/libQt*",
        "usr/lib/libgdk*",
        "usr/lib/libgtk*",
        "usr/lib/libepoxy*",
        "usr/lib/libtk*",
        "usr/lib/libwayland*",
        "usr/lib/libX11*",
        "usr/lib/libXext*",
        "usr/lib/libXinerama*",
        "usr/lib/libXmu*",
        "usr/lib/tcltk/linuxcnc",
        "usr/share/doc/linuxcnc",
    )
    for pattern in forbidden_globs:
        if any(rootfs.glob(pattern)):
            fail(f"forbidden GUI/runtime tree in rootfs: {pattern}")
    if manifest:
        for name in package_names(manifest):
            if name.startswith(FORBIDDEN_PACKAGE_PREFIXES):
                fail(f"forbidden GUI package in rootfs manifest: {name}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--package-root", type=Path, required=True)
    parser.add_argument("--rootfs", type=Path)
    parser.add_argument("--image-manifest", type=Path)
    parser.add_argument("--allowlist", type=Path, default=DEFAULT_ALLOWLIST)
    args = parser.parse_args()

    if not args.package_root.is_dir():
        fail(f"package root is missing: {args.package_root}")
    if args.rootfs and not args.rootfs.is_dir():
        fail(f"rootfs is missing: {args.rootfs}")
    if args.image_manifest and not args.image_manifest.is_file():
        fail(f"image manifest is missing: {args.image_manifest}")

    allowlist = parse_allowlist(args.allowlist)
    file_count, total_bytes = verify_package_root(args.package_root, args.allowlist)
    if args.rootfs:
        verify_rootfs(args.rootfs, args.image_manifest, allowlist)
    print(
        "V5_LINUXCNC_MINIMAL_RUNTIME_OK "
        f"files={file_count} bytes={total_bytes} rootfs={int(bool(args.rootfs))}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, UnicodeError, ValueError, RuntimeError) as exc:
        print(f"V5_LINUXCNC_MINIMAL_RUNTIME_ERROR {exc}", file=sys.stderr)
        raise SystemExit(1)
