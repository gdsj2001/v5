#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import shutil
import stat
import sys
from pathlib import Path, PurePosixPath


IDENTITY_NAME = "v5_linuxcnc_source_identity.json"
RETIRED_PATHS = (
    "board/linuxcnc/v5_linuxcnc_source.lock.json",
    "board/linuxcnc/patches/0001-v5-native-rotary-nearest-target.patch",
)


def fail(message):
    raise SystemExit(message)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_identity(source_root):
    identity_path = source_root / IDENTITY_NAME
    try:
        payload = json.loads(identity_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        fail(f"invalid LinuxCNC source identity: {exc}")
    if payload.get("schema") != "v5-linuxcnc-vendored-source-v1":
        fail("unexpected LinuxCNC source identity schema")
    upstream = payload.get("upstream", {})
    if len(upstream.get("commit", "")) != 40 or len(upstream.get("tree", "")) != 40:
        fail("LinuxCNC identity has no fixed upstream commit/tree")
    if len(payload.get("v5_source_tree", "")) != 40:
        fail("LinuxCNC identity has no fixed V5 source tree")
    symlinks = payload.get("symlinks")
    if not isinstance(symlinks, dict) or not symlinks:
        fail("LinuxCNC identity has no symlink manifest")
    if payload.get("symlink_count") != len(symlinks):
        fail("LinuxCNC identity symlink count does not match its manifest")
    return payload


def normalized_relative(relative):
    path = PurePosixPath(relative)
    if path.is_absolute() or not path.parts or any(part in ("", ".", "..") for part in path.parts):
        fail(f"invalid LinuxCNC symlink path: {relative}")
    return path


def lexical_symlink_target(source_root, path, relative, target):
    target_path = PurePosixPath(target)
    if target_path.is_absolute():
        fail(f"LinuxCNC symlink target escapes its source owner: {relative}: {target}")
    lexical_root = Path(os.path.abspath(source_root))
    lexical_target = Path(
        os.path.abspath(os.path.join(str(path.parent), *target_path.parts))
    )
    try:
        common = os.path.commonpath((str(lexical_root), str(lexical_target)))
    except ValueError:
        common = ""
    if common != str(lexical_root):
        fail(f"LinuxCNC symlink target escapes its source owner: {relative}: {target}")
    return lexical_target


def projection_tree_hash(root):
    entries = []
    for current, dirs, files in os.walk(root, followlinks=False):
        current_path = Path(current)
        relative_root = current_path.relative_to(root)
        dirs.sort()
        for name in list(dirs):
            path = current_path / name
            relative = (relative_root / name).as_posix()
            if path.is_symlink():
                dirs.remove(name)
                entries.append((relative, b"L", os.readlink(path).encode("utf-8")))
            else:
                entries.append((relative, b"D", b""))
        for name in sorted(files):
            path = current_path / name
            relative = (relative_root / name).as_posix()
            if path.is_symlink():
                entries.append((relative, b"L", os.readlink(path).encode("utf-8")))
            elif path.is_file():
                entries.append((relative, b"F", path.read_bytes()))
            else:
                fail(f"unsupported flattened LinuxCNC projection entry: {path}")
    digest = hashlib.sha256()
    for relative, kind, content in sorted(entries):
        digest.update(kind + b"\0" + relative.encode("utf-8") + b"\0")
        digest.update(str(len(content)).encode("ascii") + b"\0" + content)
    return digest.hexdigest()


def validate_symlinks(source_root, symlinks, allow_flattened):
    manifest_paths = set()
    for relative, target in symlinks.items():
        relative_path = normalized_relative(relative)
        if not isinstance(target, str) or not target:
            fail(f"invalid LinuxCNC symlink target: {relative}")
        path = source_root.joinpath(*relative_path.parts)
        target_path = lexical_symlink_target(source_root, path, relative, target)
        manifest_paths.add(relative_path.as_posix())
        if path.is_symlink():
            actual = os.readlink(path).replace("\\", "/")
            if actual != target:
                fail(f"LinuxCNC symlink target mismatch: {relative}: {actual} != {target}")
            continue
        if not allow_flattened:
            fail(f"LinuxCNC source path is not the registered symlink: {relative}")
        try:
            metadata = os.lstat(path)
        except OSError as exc:
            fail(f"flattened LinuxCNC symlink is missing: {relative}: {exc}")
        if stat.S_ISREG(metadata.st_mode):
            if metadata.st_size == 0:
                continue
            if target_path.is_file() and sha256(path) == sha256(target_path):
                continue
            fail(f"flattened LinuxCNC file projection does not match its target: {relative}")
        if stat.S_ISDIR(metadata.st_mode):
            try:
                if not any(path.iterdir()):
                    continue
            except OSError as exc:
                fail(f"flattened LinuxCNC symlink directory is unreadable: {relative}: {exc}")
            if target_path.is_dir() and projection_tree_hash(path) == projection_tree_hash(target_path):
                continue
            fail(f"flattened LinuxCNC directory projection does not match its target: {relative}")
        fail(f"unsupported flattened LinuxCNC symlink projection: {relative}")

    if not allow_flattened:
        actual_paths = set()
        for current, dirs, files in os.walk(source_root, followlinks=False):
            current_path = Path(current)
            for name in list(dirs) + files:
                path = current_path / name
                if path.is_symlink():
                    actual_paths.add(path.relative_to(source_root).as_posix())
        unregistered = sorted(actual_paths - manifest_paths)
        if unregistered:
            fail(f"LinuxCNC source has unregistered symlinks: {unregistered[0]}")


def source_entries(source_root, symlinks):
    entries = []
    symlink_paths = set(symlinks)
    for current, dirs, files in os.walk(source_root, followlinks=False):
        current_path = Path(current)
        for name in list(dirs):
            path = current_path / name
            relative = path.relative_to(source_root).as_posix()
            if relative in symlink_paths:
                dirs.remove(name)
                continue
            if path.is_symlink():
                fail(f"LinuxCNC source has an unregistered symlink: {relative}")
        for name in files:
            path = current_path / name
            if path.name == IDENTITY_NAME or path.suffix == ".pyc":
                continue
            if "__pycache__" in path.parts or ".git" in path.parts:
                continue
            relative = path.relative_to(source_root).as_posix()
            if relative in symlink_paths:
                continue
            if path.is_symlink():
                fail(f"LinuxCNC source has an unregistered symlink: {relative}")
            entries.append((relative, b"F", path))
    for relative, target in symlinks.items():
        entries.append((relative, b"L", target.encode("utf-8")))
    return sorted(entries, key=lambda item: item[0])


def source_content_hash(source_root, symlinks):
    digest = hashlib.sha256()
    for relative, kind, source in source_entries(source_root, symlinks):
        content = source if kind == b"L" else source.read_bytes()
        digest.update(kind + b"\0" + relative.encode("utf-8") + b"\0")
        digest.update(str(len(content)).encode("ascii") + b"\0" + content)
    return digest.hexdigest()


def materialize_symlinks(source_root, target_root, symlinks):
    source_resolved = source_root.resolve()
    target_resolved = target_root.resolve()
    if os.path.commonpath((str(source_resolved), str(target_resolved))) == str(source_resolved):
        fail("refusing to materialize symlinks inside the Windows source owner")
    for relative, target in sorted(symlinks.items()):
        relative_path = normalized_relative(relative)
        path = target_root.joinpath(*relative_path.parts)
        if os.path.lexists(path):
            if path.is_dir() and not path.is_symlink():
                shutil.rmtree(path)
            else:
                path.unlink()
        path.parent.mkdir(parents=True, exist_ok=True)
        os.symlink(target, path)


def validate_tokens(source_root, identity):
    for relative, tokens in identity.get("required_tokens", {}).items():
        text = (source_root / relative).read_text(encoding="utf-8", errors="strict")
        for token in tokens:
            if token not in text:
                fail(f"LinuxCNC source is missing required token {token}: {relative}")
    for relative, tokens in identity.get("forbidden_tokens", {}).items():
        text = (source_root / relative).read_text(encoding="utf-8", errors="strict")
        for token in tokens:
            if token in text:
                fail(f"LinuxCNC source contains retired token {token}: {relative}")


def validate_source(project_root, source_root, identity, print_hash, allow_flattened_symlinks):
    if not source_root.is_dir():
        fail(f"LinuxCNC source root is missing: {source_root}")
    if (source_root / ".git").exists():
        fail("LinuxCNC source must be owned by the main project Git, not a nested repository")
    for relative in RETIRED_PATHS:
        if (project_root / relative).exists():
            fail(f"retired duplicate LinuxCNC implementation still exists: {relative}")
    symlinks = identity["symlinks"]
    validate_symlinks(source_root, symlinks, allow_flattened_symlinks)
    validate_tokens(source_root, identity)
    actual = source_content_hash(source_root, symlinks)
    if print_hash:
        print(actual)
        return actual
    expected = identity.get("content_sha256", "")
    if len(expected) != 64 or actual != expected:
        fail(f"LinuxCNC source content hash mismatch: {actual} != {expected}")
    return actual


def validate_elf_arm(path):
    header = path.read_bytes()[:20]
    if len(header) < 20 or header[:4] != b"\x7fELF":
        fail(f"native artifact is not ELF: {path}")
    if header[4] != 1 or header[5] != 1:
        fail(f"native artifact is not 32-bit little-endian ELF: {path}")
    machine = int.from_bytes(header[18:20], "little")
    if machine != 40:
        fail(f"native artifact is not ARM ELF: {path}: e_machine={machine}")


def validate_artifacts(identity, artifact_root):
    records = identity.get("artifacts", [])
    results = []
    for record in records:
        relative = record["path"]
        path = artifact_root / relative
        if not path.is_file():
            fail(f"missing LinuxCNC native artifact: {path}")
        validate_elf_arm(path)
        content = path.read_bytes()
        for token in record.get("required_ascii", []):
            if token.encode("ascii") not in content:
                fail(f"native artifact is missing token {token}: {relative}")
        for token in record.get("forbidden_ascii", []):
            if token.encode("ascii") in content:
                fail(f"native artifact contains retired token {token}: {relative}")
        results.append({"path": relative, "size": path.stat().st_size, "sha256": sha256(path)})
    return results


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-root", type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument("--source-root", type=Path)
    parser.add_argument("--print-source-hash", action="store_true")
    parser.add_argument("--allow-flattened-symlinks", action="store_true")
    parser.add_argument("--materialize-symlinks", type=Path)
    parser.add_argument("--artifact-root", type=Path)
    parser.add_argument("--write-artifact-identity", type=Path)
    args = parser.parse_args()

    project_root = args.project_root.resolve()
    source_root = (args.source_root or project_root / "linuxcnc").resolve()
    identity = load_identity(source_root)
    actual_hash = validate_source(
        project_root,
        source_root,
        identity,
        args.print_source_hash,
        args.allow_flattened_symlinks,
    )
    if args.materialize_symlinks:
        materialize_symlinks(source_root, args.materialize_symlinks.resolve(), identity["symlinks"])
    artifacts = []
    if args.artifact_root:
        artifacts = validate_artifacts(identity, args.artifact_root.resolve())
    if args.write_artifact_identity:
        if not args.artifact_root:
            fail("--write-artifact-identity requires --artifact-root")
        payload = {
            "schema": "v5-linuxcnc-artifact-identity-v2",
            "source_content_sha256": actual_hash,
            "upstream": identity["upstream"],
            "artifacts": artifacts,
        }
        args.write_artifact_identity.parent.mkdir(parents=True, exist_ok=True)
        args.write_artifact_identity.write_text(
            json.dumps(payload, ensure_ascii=True, indent=2) + "\n",
            encoding="ascii",
        )
    if not args.print_source_hash:
        print(
            "V5_LINUXCNC_SOURCE_OK "
            f"commit={identity['upstream']['commit']} content_sha256={actual_hash} "
            f"artifacts={len(artifacts)}"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
