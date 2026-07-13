#!/usr/bin/env python3
"""Verify the Windows-owned PetaLinux source package closure."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


SCHEMA = "v5-petalinux-source-packages-v1"
DEFAULT_SOURCE_ROOT = Path("board/third_party/petalinux-source-packages")
DEFAULT_INVENTORY = Path("board/petalinux/v5_bitbake_source_inventory.json")
MANIFEST_NAME = "v5_source_packages.json"
FORBIDDEN_NAMES = {".git", ".gitattributes", ".gitignore", ".mailmap"}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def payload_entries(source_root: Path) -> list[dict[str, object]]:
    if not source_root.is_dir():
        raise RuntimeError(f"source package root is missing: {source_root}")

    entries: list[dict[str, object]] = []
    for path in sorted(source_root.iterdir(), key=lambda item: item.name):
        if path.name == MANIFEST_NAME:
            continue
        if path.name in FORBIDDEN_NAMES or path.name == ".github":
            raise RuntimeError(f"Git control metadata is forbidden: {path}")
        if path.is_dir():
            raise RuntimeError(f"source packages must remain flat archives: {path}")
        if not path.is_file():
            raise RuntimeError(f"unsupported source package entry: {path}")
        if path.name.endswith((".lock", ".bad-checksum")):
            raise RuntimeError(f"transient download state is forbidden: {path}")
        entries.append(
            {
                "path": path.name,
                "size": path.stat().st_size,
                "sha256": sha256_file(path),
            }
        )
    if not entries:
        raise RuntimeError(f"source package root is empty: {source_root}")
    return entries


def tree_sha256(entries: list[dict[str, object]]) -> str:
    digest = hashlib.sha256()
    for entry in entries:
        digest.update(str(entry["path"]).encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(entry["size"]).encode("ascii"))
        digest.update(b"\0")
        digest.update(str(entry["sha256"]).encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def load_inventory(project_root: Path) -> tuple[dict[str, object], str]:
    path = project_root / DEFAULT_INVENTORY
    if not path.is_file():
        raise RuntimeError(f"BitBake source inventory is missing: {path}")
    payload = json.loads(path.read_text(encoding="utf-8-sig"))
    if payload.get("schema") != "v5-bitbake-source-inventory-v1":
        raise RuntimeError("unexpected BitBake source inventory schema")
    return payload, sha256_file(path)


def make_manifest(project_root: Path, source_root: Path) -> dict[str, object]:
    entries = payload_entries(source_root)
    inventory, inventory_hash = load_inventory(project_root)
    expected_names = {entry["path"] for entry in inventory["source_packages"]}
    actual_names = {entry["path"] for entry in entries}
    if actual_names != expected_names:
        missing = sorted(expected_names - actual_names)
        extra = sorted(actual_names - expected_names)
        raise RuntimeError(
            "source package set does not match BitBake inventory: "
            f"missing={missing} extra={extra}"
        )
    return {
        "schema": SCHEMA,
        "source_owner": source_root.relative_to(project_root).as_posix(),
        "petalinux_version": "2020.2",
        "generated_from": "petalinux-image-minimal --runall=fetch",
        "inventory_path": DEFAULT_INVENTORY.as_posix(),
        "inventory_sha256": inventory_hash,
        "recipe_count": inventory["recipe_count"],
        "package_count": len(entries),
        "total_size": sum(int(entry["size"]) for entry in entries),
        "tree_sha256": tree_sha256(entries),
        "files": entries,
    }


def write_manifest(project_root: Path, source_root: Path) -> dict[str, object]:
    manifest = make_manifest(project_root, source_root)
    target = source_root / MANIFEST_NAME
    target.write_text(
        json.dumps(manifest, indent=2, ensure_ascii=True) + "\n",
        encoding="utf-8",
    )
    return manifest


def verify_manifest(project_root: Path, source_root: Path) -> dict[str, object]:
    manifest_path = source_root / MANIFEST_NAME
    if not manifest_path.is_file():
        raise RuntimeError(f"source package manifest is missing: {manifest_path}")
    expected = json.loads(manifest_path.read_text(encoding="utf-8"))
    actual = make_manifest(project_root, source_root)
    if expected != actual:
        raise RuntimeError("source package manifest does not match the Windows payload")
    return actual


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-root", type=Path, default=Path.cwd())
    parser.add_argument("--source-root", type=Path)
    parser.add_argument("--write-manifest", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    project_root = args.project_root.resolve()
    source_root = (
        args.source_root.resolve()
        if args.source_root
        else (project_root / DEFAULT_SOURCE_ROOT).resolve()
    )
    if project_root not in source_root.parents:
        raise RuntimeError(f"source package root escaped project root: {source_root}")

    manifest = (
        write_manifest(project_root, source_root)
        if args.write_manifest
        else verify_manifest(project_root, source_root)
    )
    print(
        "V5_SOURCE_PACKAGES_OK "
        f"count={manifest['package_count']} total_size={manifest['total_size']} "
        f"tree_sha256={manifest['tree_sha256']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
