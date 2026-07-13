#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import re
from pathlib import Path


OWNERS = (
    {
        "relative": "linux/kernel",
        "identity": "v5_linux_source_identity.json",
        "schema": "v5-linux-kernel-vendored-source-v1",
        "owner": "linux/kernel",
        "required": ("Makefile", "COPYING", "mm/page_alloc.c"),
    },
    {
        "relative": "linux/realtime",
        "identity": "v5_realtime_source_identity.json",
        "schema": "v5-linux-realtime-metadata-v1",
        "owner": "linux/realtime",
        "required": (
            "features/rt/rt.scc",
            "features/rt/Use-CONFIG_PREEMPTION.patch",
            "features/rt/workqueue-Convert-the-locks-to-raw-type.patch",
            "features/rt/mm-page_alloc-rt-friendly-per-cpu-pages.patch",
        ),
    },
)


def fail(message):
    raise SystemExit(message)


def source_entries(source_root, identity_name):
    entries = []
    for current, dirs, files in os.walk(source_root, followlinks=False):
        current_path = Path(current)
        if ".git" in current_path.parts:
            dirs[:] = []
            continue
        for name in list(dirs):
            path = current_path / name
            if path.is_symlink():
                dirs.remove(name)
                entries.append(path)
            elif name == ".git":
                fail("nested Git owner is forbidden: %s" % path)
        for name in files:
            path = current_path / name
            if name == identity_name or ".git" in path.parts:
                continue
            entries.append(path)
    return sorted(entries, key=lambda item: item.relative_to(source_root).as_posix())


def source_content_hash(source_root, entries):
    digest = hashlib.sha256()
    symlink_count = 0
    for path in entries:
        relative = path.relative_to(source_root).as_posix().encode("utf-8")
        if path.is_symlink():
            kind = b"L"
            content = os.readlink(path).replace("\\", "/").encode("utf-8")
            symlink_count += 1
        else:
            kind = b"F"
            content = path.read_bytes()
        digest.update(kind + b"\0" + relative + b"\0")
        digest.update(str(len(content)).encode("ascii") + b"\0" + content)
    return digest.hexdigest(), symlink_count


def load_identity(source_root, owner):
    path = source_root / owner["identity"]
    try:
        identity = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        fail("invalid Linux source identity %s: %s" % (path, exc))
    if identity.get("schema") != owner["schema"]:
        fail("unexpected identity schema: %s" % path)
    if identity.get("source_owner") != owner["owner"]:
        fail("unexpected source owner: %s" % path)
    upstream = identity.get("upstream", {})
    for field in ("url", "commit", "tree"):
        value = upstream.get(field, "")
        if not value:
            fail("missing upstream %s: %s" % (field, path))
    for field in ("commit", "tree"):
        if not re.fullmatch(r"[0-9a-f]{40}", upstream[field]):
            fail("invalid upstream %s: %s" % (field, path))
    inaccessible = identity.get("vm_share_inaccessible_paths", [])
    if not isinstance(inaccessible, list) or len(inaccessible) != len(set(inaccessible)):
        fail("invalid vm_share_inaccessible_paths: %s" % path)
    for relative in inaccessible:
        candidate = Path(relative)
        if candidate.is_absolute() or ".." in candidate.parts:
            fail("VM-share-inaccessible path escaped its owner: %s" % relative)
        if os.name == "nt" and not (source_root / candidate).is_file():
            fail("VM-share-inaccessible Windows source is missing: %s" % relative)
    return identity


def validate_rt_contract(source_root):
    rt_scc = (source_root / "features/rt/rt.scc").read_text(encoding="utf-8")
    active = [line.strip() for line in rt_scc.splitlines() if line.strip().startswith("patch ")]
    page_patch = "patch mm-page_alloc-rt-friendly-per-cpu-pages.patch"
    if active.count(page_patch) != 1:
        fail("RT page allocator locking patch must be active exactly once")
    forbidden = (
        "patch arm-remove-printk_nmi_.patch",
        "patch Split-IRQ-off-and-zone-lock-while-freeing-pages-from.patch",
    )
    for line in forbidden:
        if line in active:
            fail("incompatible RT patch remains active: %s" % line)
    for relative in (
        "features/rt/Use-CONFIG_PREEMPTION.patch",
        "features/rt/workqueue-Convert-the-locks-to-raw-type.patch",
        "features/rt/mm-page_alloc-rt-friendly-per-cpu-pages.patch",
    ):
        text = (source_root / relative).read_text(encoding="utf-8")
        if "V5-Xilinx-Adaptation:" not in text:
            fail("missing registered Xilinx RT adaptation: %s" % relative)


def verify_owner(project_root, owner, print_hashes):
    source_root = project_root / owner["relative"]
    if not source_root.is_dir():
        fail("missing Linux source owner: %s" % source_root)
    identity = load_identity(source_root, owner)
    for relative in owner["required"]:
        if not (source_root / relative).is_file():
            fail("missing Linux source input: %s/%s" % (owner["relative"], relative))
    entries = source_entries(source_root, owner["identity"])
    actual_hash, symlink_count = source_content_hash(source_root, entries)
    if not print_hashes:
        expected = (
            identity.get("content_sha256"),
            identity.get("file_count"),
            identity.get("symlink_count"),
        )
        actual = (actual_hash, len(entries), symlink_count)
        if actual != expected:
            fail("Linux source identity mismatch %s: %r != %r" % (owner["relative"], actual, expected))
    print(
        "V5_LINUX_OWNER source=%s content_sha256=%s files=%d symlinks=%d"
        % (owner["relative"], actual_hash, len(entries), symlink_count)
    )
    return actual_hash


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-root", type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument("--print-source-hashes", action="store_true")
    parser.add_argument("--build-root", type=Path)
    parser.add_argument("--projection-root", type=Path)
    args = parser.parse_args()

    project_root = args.project_root.resolve()
    if os.name != "nt":
        if not args.build_root or not args.projection_root:
            fail("Linux verification requires build and projection roots")
        import project_v5_linux_source as projection

        projection.project_and_verify(
            project_root,
            args.build_root,
            args.projection_root,
            clean_after=True,
        )
        return 0
    hashes = {}
    for owner in OWNERS:
        hashes[owner["relative"]] = verify_owner(project_root, owner, args.print_source_hashes)
    validate_rt_contract(project_root / "linux/realtime")
    if not args.print_source_hashes:
        print(
            "V5_LINUX_SOURCE_OK kernel=%s realtime=%s"
            % (hashes["linux/kernel"], hashes["linux/realtime"])
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
