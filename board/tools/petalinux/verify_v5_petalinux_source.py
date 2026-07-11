#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import re
import sys
from pathlib import Path


IDENTITY_NAME = "v5_petalinux_source_identity.json"
REQUIRED_PATHS = (
    "config.project",
    ".petalinux/metadata",
    "project-spec/attributes",
    "project-spec/configs/config",
    "project-spec/configs/rootfs_config",
    "project-spec/hw-description/system.bit",
    "project-spec/hw-description/system.xsa",
    "project-spec/meta-user/conf/layer.conf",
    "project-spec/meta-user/conf/user-rootfsconfig",
    "project-spec/meta-user/recipes-apps/v5-base-overlay/v5-base-overlay.bb",
    "project-spec/meta-user/recipes-apps/v5-stepgen-module/v5-stepgen-module.bb",
    "project-spec/meta-user/recipes-apps/v5-stepgen-module/files/zynq_stepgen_hw.c",
    "project-spec/meta-user/recipes-apps/linuxcnc-ethercat/linuxcnc-ethercat_git.bb",
    "project-spec/meta-user/recipes-devtools/sip/sip3_4.19.16.bbappend",
    "project-spec/meta-user/recipes-python/pyqt5/python3-pyqt5_5.12.1.bbappend",
    "project-spec/meta-user/recipes-kernel/ethercat-master/ethercat-master_git.bb",
    "project-spec/meta-user/recipes-kernel/linux/linux-xlnx_%.bbappend",
)
RETIRED_PATHS = (
    "project-spec/meta-user/recipes-apps/gpio-demo",
    "project-spec/meta-user/recipes-apps/peekpoke",
    "project-spec/meta-user/recipes-apps/z20-cnc-overlay",
    "project-spec/meta-user/recipes-apps/z20-stepgen-module",
    "build",
    "components",
)
PREBUILT_SUFFIXES = (".a", ".ko", ".o", ".so")
ARCHIVE_SUFFIXES = (".7z", ".rar", ".tar", ".tar.gz", ".tgz", ".zip")
LOCAL_FILE_RE = re.compile(r"file://([^\s\\\"']+)")
Z20_EXTERNAL_FACT_PATHS = {
    "project-spec/configs/config",
    "project-spec/meta-user/recipes-bsp/device-tree/files/system-top.dts",
}


def fail(message):
    raise SystemExit(message)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_identity(source_root):
    path = source_root / IDENTITY_NAME
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        fail("invalid PetaLinux source identity: %s" % exc)
    if payload.get("schema") != "v5-petalinux-project-source-v1":
        fail("unexpected PetaLinux source identity schema")
    if payload.get("source_owner") != "board/petalinux":
        fail("unexpected PetaLinux source owner")
    if payload.get("petalinux_version") != "2020.2":
        fail("unexpected PetaLinux version")
    return payload


def source_entries(source_root):
    entries = []
    for current, dirs, files in os.walk(source_root, followlinks=False):
        current_path = Path(current)
        for name in list(dirs):
            path = current_path / name
            if path.is_symlink():
                dirs.remove(name)
                entries.append(path)
        for name in files:
            path = current_path / name
            if path.name == IDENTITY_NAME or path.suffix == ".pyc":
                continue
            if "__pycache__" in path.parts or ".git" in path.parts:
                continue
            entries.append(path)
    return sorted(entries, key=lambda item: item.relative_to(source_root).as_posix())


def source_content_hash(source_root, entries):
    digest = hashlib.sha256()
    for path in entries:
        relative = path.relative_to(source_root).as_posix().encode("utf-8")
        if path.is_symlink():
            kind = b"L"
            content = os.readlink(path).encode("utf-8")
        else:
            kind = b"F"
            content = path.read_bytes()
        digest.update(kind + b"\0" + relative + b"\0")
        digest.update(str(len(content)).encode("ascii") + b"\0" + content)
    return digest.hexdigest()


def is_backup_name(name):
    lower = name.lower()
    return (
        lower in {"bak", "backup", "old", "_sync_reports", "__pycache__"}
        or lower.startswith(("bak_", "backup_", "old_"))
        or lower.endswith((".bak", ".backup", ".old", ".before"))
        or ".bak_" in lower
        or ".backup_" in lower
        or ".old_" in lower
        or ".before_" in lower
    )


def validate_layout(source_root, entries):
    if (source_root / ".git").exists():
        fail("PetaLinux source must be owned by the main project Git")
    for relative in REQUIRED_PATHS:
        if not (source_root / relative).is_file():
            fail("missing PetaLinux source input: %s" % relative)
    for relative in RETIRED_PATHS:
        if (source_root / relative).exists():
            fail("retired PetaLinux path still exists: %s" % relative)
    for path in entries:
        relative = path.relative_to(source_root).as_posix()
        if path.is_symlink():
            fail("PetaLinux source contains a symlink: %s" % relative)
        if any(is_backup_name(part) for part in path.relative_to(source_root).parts):
            fail("PetaLinux source contains backup/cache material: %s" % relative)
        lower = relative.lower()
        if any(lower.endswith(suffix) for suffix in PREBUILT_SUFFIXES):
            fail("PetaLinux source contains a prebuilt object: %s" % relative)
        if any(lower.endswith(suffix) for suffix in ARCHIVE_SUFFIXES):
            fail("PetaLinux source contains a source/archive copy: %s" % relative)
        if "z20" in path.name.lower():
            fail("maintainable PetaLinux path still uses retired z20 naming: %s" % relative)


def validate_text_boundaries(source_root, entries):
    for path in entries:
        content = path.read_bytes()
        if b"\0" in content:
            continue
        relative = path.relative_to(source_root).as_posix()
        text = content.decode("utf-8", errors="strict")
        lower = text.lower()
        if "z20" in lower and relative not in Z20_EXTERNAL_FACT_PATHS:
            fail("PetaLinux source contains retired z20 product naming: %s" % relative)
        if "codex" in lower:
            fail("PetaLinux product source contains an internal Codex label: %s" % relative)
        for token in ("/home/sj/", "/root/Desktop/", "D:/v5", "D:\\v5"):
            if token in text:
                fail("PetaLinux source contains an absolute project/source path: %s" % relative)


def resolve_local_file(recipe, token):
    token = token.split(";", 1)[0]
    if "$" in token:
        return True
    candidates = [recipe.parent / token, recipe.parent / "files" / token]
    for child in recipe.parent.iterdir():
        if child.is_dir():
            candidates.append(child / token)
    return any(candidate.exists() for candidate in candidates)


def validate_recipe_inputs(source_root):
    recipe_root = source_root / "project-spec/meta-user"
    for recipe in sorted(recipe_root.rglob("*.bb")) + sorted(recipe_root.rglob("*.bbappend")):
        text = recipe.read_text(encoding="utf-8", errors="strict")
        src_uri_lines = []
        collecting = False
        for line in text.splitlines():
            if re.match(r"^SRC_URI(?:_[A-Za-z0-9${}:+.-]+)?\s*(?:\+?=)", line):
                collecting = True
            if collecting:
                src_uri_lines.append(line)
                if not line.rstrip().endswith("\\"):
                    collecting = False
        for token in LOCAL_FILE_RE.findall("\n".join(src_uri_lines)):
            if not resolve_local_file(recipe, token):
                fail("recipe local input is missing: %s: file://%s" % (recipe, token))


def require_tokens(source_root, relative, required, forbidden=()):
    text = (source_root / relative).read_text(encoding="utf-8", errors="strict")
    for token in required:
        if token not in text:
            fail("PetaLinux source is missing required token %r: %s" % (token, relative))
    for token in forbidden:
        if token in text:
            fail("PetaLinux source contains retired token %r: %s" % (token, relative))


def validate_contract(source_root):
    require_tokens(
        source_root,
        "project-spec/configs/config",
        (
            'CONFIG_YOCTO_BB_NUMBER_THREADS="1"',
            'CONFIG_YOCTO_PARALLEL_MAKE="2"',
            'CONFIG_SUBSYSTEM_ROOTFS_EXT4=y',
            'CONFIG_SUBSYSTEM_SDROOT_DEV="/dev/mmcblk0p2"',
            'CONFIG_SUBSYSTEM_RFS_FORMATS="tar.gz ext4"',
        ),
        ("CONFIG_SUBSYSTEM_ROOTFS_INITRD=y",),
    )
    require_tokens(
        source_root,
        "project-spec/configs/rootfs_config",
        ("CONFIG_v5-base-overlay=y", "CONFIG_v5-stepgen-module=y"),
        ("CONFIG_z20", "CONFIG_v5-cnc-overlay"),
    )
    require_tokens(
        source_root,
        "project-spec/meta-user/recipes-apps/v5-stepgen-module/v5-stepgen-module.bb",
        ("do_compile()", "zynq_stepgen_hw.c", "${B}/zynq_stepgen_hw.so"),
        ("file://zynq_stepgen_hw.so", "do_compile[noexec]"),
    )
    require_tokens(
        source_root,
        "project-spec/meta-user/recipes-apps/v5-stepgen-module/files/zynq_stepgen_hw.c",
        ("V5_STEPGEN_BUILD_TAG", "V5_STEPGEN_UIO_DEVICE", "/dev/v5-stepgen-uio"),
        ("Z20_STEPGEN", "/dev/z20-stepgen-uio"),
    )
    require_tokens(
        source_root,
        "project-spec/meta-user/recipes-bsp/u-boot/u-boot-zynq-scr/boot.cmd.default.ext4",
        (
            "root=/dev/mmcblk0p2 rw rootwait",
            "8ax,pl-dna-source",
            "8ax,pl-dna-value",
            "refusing unverified FIT boot",
        ),
        (
            "root=/dev/ram0",
            "bootm ramdisk",
            "z20,pl-dna",
            "v5,pl-dna",
            "falling back to normal FIT boot",
        ),
    )
    require_tokens(
        source_root,
        "project-spec/meta-user/recipes-kernel/ethercat-master/ethercat-master_git.bb",
        ('MASTER0_DEVICE="eth1"',),
        ('MASTER0_DEVICE="eth0"',),
    )
    require_tokens(
        source_root,
        "project-spec/meta-user/recipes-devtools/sip/sip3_4.19.16.bbappend",
        (
            "https://downloads.yoctoproject.org/mirror/sources/",
            "sip-${PV}.tar.gz",
        ),
        ("www.riverbankcomputing.com",),
    )
    require_tokens(
        source_root,
        "project-spec/meta-user/recipes-python/pyqt5/python3-pyqt5_5.12.1.bbappend",
        (
            "https://files.kde.org/krita/build/dependencies/",
            "PyQt5_gpl-${PV}.tar.gz",
        ),
        ("www.riverbankcomputing.com",),
    )
    for relative in (
        "project-spec/meta-user/recipes-apps/v5-base-overlay/files/network/v5_net_core.sh",
        "project-spec/meta-user/recipes-apps/v5-base-overlay/files/network/v5_wifi_core.sh",
    ):
        if len((source_root / relative).read_text(encoding="utf-8").splitlines()) > 500:
            fail("self-written PetaLinux network module exceeds 500 lines: %s" % relative)


def validate_identity(source_root, identity, entries, print_hash):
    hardware = identity.get("hardware", {})
    for key in ("xsa", "bit"):
        path = source_root / hardware.get(key + "_path", "")
        expected = hardware.get(key + "_sha256", "")
        if not path.is_file() or sha256(path) != expected:
            fail("PetaLinux hardware identity mismatch: %s" % key)
    actual_hash = source_content_hash(source_root, entries)
    if print_hash:
        print("%s %d" % (actual_hash, len(entries)))
        return actual_hash
    expected_hash = identity.get("content_sha256", "")
    expected_count = identity.get("file_count", 0)
    if actual_hash != expected_hash or len(entries) != expected_count:
        fail(
            "PetaLinux source identity mismatch: %s/%d != %s/%d"
            % (actual_hash, len(entries), expected_hash, expected_count)
        )
    return actual_hash


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-root", type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument("--source-root", type=Path)
    parser.add_argument("--print-source-hash", action="store_true")
    args = parser.parse_args()

    project_root = args.project_root.resolve()
    source_root = (args.source_root or project_root / "board/petalinux").resolve()
    identity = load_identity(source_root)
    entries = source_entries(source_root)
    validate_layout(source_root, entries)
    validate_text_boundaries(source_root, entries)
    validate_recipe_inputs(source_root)
    validate_contract(source_root)
    actual_hash = validate_identity(source_root, identity, entries, args.print_source_hash)
    if not args.print_source_hash:
        print(
            "V5_PETALINUX_SOURCE_OK version=%s content_sha256=%s files=%d"
            % (identity["petalinux_version"], actual_hash, len(entries))
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
