#!/usr/bin/env python3
"""Create and verify a complete regular-file/symlink rootfs manifest."""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import stat
import sys
import tempfile
from typing import Dict, Iterable, List, NamedTuple


HEADER = "# type\tmode\tsize\tsha256\tpath\tlink_target"


class FileRecord(NamedTuple):
    kind: str
    mode: str
    size: int
    sha256: str
    path: str
    link_target: str


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def safe_text(value: str, field: str) -> str:
    if not value or any(character in value for character in ("\t", "\r", "\n")):
        raise ValueError(f"invalid {field}: {value!r}")
    return value


def record_for(path: Path, root: Path) -> FileRecord:
    relative = safe_text(path.relative_to(root).as_posix(), "path")
    metadata = path.lstat()
    mode = f"{stat.S_IMODE(metadata.st_mode):04o}"
    if stat.S_ISLNK(metadata.st_mode):
        target = safe_text(os.readlink(path), "link target")
        encoded = target.encode("utf-8")
        return FileRecord("symlink", mode, len(encoded), sha256_bytes(encoded), relative, target)
    if stat.S_ISREG(metadata.st_mode):
        return FileRecord("file", mode, metadata.st_size, sha256_file(path), relative, "")
    raise ValueError(f"unsupported filesystem object in rootfs: {relative}")


def iter_objects(root: Path) -> Iterable[Path]:
    for current_text, directory_names, file_names in os.walk(root, followlinks=False):
        current = Path(current_text)
        for name in list(directory_names):
            path = current / name
            if path.is_symlink():
                directory_names.remove(name)
                yield path
        for name in file_names:
            yield current / name


def collect(root: Path) -> Dict[str, FileRecord]:
    root = root.resolve()
    if not root.is_dir():
        raise ValueError(f"rootfs directory is missing: {root}")
    result: Dict[str, FileRecord] = {}
    for path in iter_objects(root):
        record = record_for(path, root)
        if record.path in result:
            raise ValueError(f"duplicate rootfs path: {record.path}")
        result[record.path] = record
    return result


def write_manifest(root: Path, output: Path) -> int:
    records = collect(root)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(HEADER + "\n")
        for path in sorted(records):
            record = records[path]
            stream.write(
                f"{record.kind}\t{record.mode}\t{record.size}\t{record.sha256}\t"
                f"{record.path}\t{record.link_target}\n"
            )
    return len(records)


def read_manifest(path: Path) -> Dict[str, FileRecord]:
    result: Dict[str, FileRecord] = {}
    for line_number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not raw or raw.startswith("#"):
            continue
        fields = raw.split("\t")
        if len(fields) != 6:
            raise ValueError(f"manifest line {line_number}: expected 6 tab fields")
        kind, mode, size_text, digest, relative, link_target = fields
        if kind not in {"file", "symlink"}:
            raise ValueError(f"manifest line {line_number}: invalid type {kind!r}")
        if relative in result:
            raise ValueError(f"manifest line {line_number}: duplicate path {relative}")
        result[relative] = FileRecord(
            kind, mode, int(size_text), digest, safe_text(relative, "path"), link_target
        )
    return result


def verify_manifest(root: Path, manifest: Path) -> List[str]:
    expected = read_manifest(manifest)
    actual = collect(root)
    errors: List[str] = []
    for missing in sorted(set(expected) - set(actual)):
        errors.append(f"missing:{missing}")
    for extra in sorted(set(actual) - set(expected)):
        errors.append(f"extra:{extra}")
    for path in sorted(set(expected).intersection(actual)):
        if expected[path] != actual[path]:
            errors.append(f"mismatch:{path}:expected={expected[path]}:actual={actual[path]}")
    return errors


def self_test() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        base = Path(temporary)
        root = base / "rootfs"
        root.mkdir()
        regular = root / "usr/bin/example"
        regular.parent.mkdir(parents=True)
        regular.write_bytes(b"example\n")
        regular.chmod(0o755)
        link = root / "usr/bin/example-link"
        try:
            link.symlink_to("example")
        except OSError:
            link = None
        manifest = base / "manifest.tsv"
        count = write_manifest(root, manifest)
        assert count == (2 if link is not None else 1)
        assert not verify_manifest(root, manifest)
        regular.write_bytes(b"changed\n")
        assert any(error.startswith("mismatch:usr/bin/example") for error in verify_manifest(root, manifest))
    print("V5_PRODUCT_FILE_MANIFEST_SELF_TEST_OK")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("create", "verify", "self-test"))
    parser.add_argument("--root", type=Path)
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()
    if args.command == "self-test":
        self_test()
        return 0
    if args.root is None or args.manifest is None:
        parser.error("--root and --manifest are required")
    try:
        if args.command == "create":
            count = write_manifest(args.root, args.manifest)
            print(f"V5_PRODUCT_FILE_MANIFEST_CREATED files={count} path={args.manifest}")
            return 0
        errors = verify_manifest(args.root, args.manifest)
    except (OSError, ValueError) as exc:
        print(f"V5_PRODUCT_FILE_MANIFEST_FAILED: {exc}", file=sys.stderr)
        return 1
    if errors:
        for error in errors[:50]:
            print(f"V5_PRODUCT_FILE_MANIFEST_MISMATCH: {error}", file=sys.stderr)
        if len(errors) > 50:
            print(
                f"V5_PRODUCT_FILE_MANIFEST_MISMATCH: additional={len(errors) - 50}",
                file=sys.stderr,
            )
        return 1
    print(f"V5_PRODUCT_FILE_MANIFEST_OK path={args.manifest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
