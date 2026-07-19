#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import json
import tempfile
from pathlib import Path


HERE = Path(__file__).resolve().parent
VERIFY_PATH = HERE / "verify_v5_petalinux_source.py"
PROJECT_ROOT = HERE.parents[2]
ROOTFS_PATH = PROJECT_ROOT / "board" / "petalinux" / "project-spec" / "configs" / "rootfs_config"
INVENTORY_PATH = PROJECT_ROOT / "board" / "petalinux" / "v5_bitbake_source_inventory.json"
MANIFEST_PATH = PROJECT_ROOT / "board" / "third_party" / "petalinux-source-packages" / "v5_source_packages.json"


def load_verifier():
    spec = importlib.util.spec_from_file_location("v5_petalinux_source_verifier", VERIFY_PATH)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def write_text(path: Path, text: str) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(text)


def write_json(path: Path, payload) -> None:
    write_text(path, json.dumps(payload, sort_keys=True) + "\n")


def make_fixture(root: Path, verifier):
    rootfs = root / "rootfs_config"
    inventory = root / "inventory.json"
    manifest = root / "manifest.json"
    archive = root / verifier.TCF_ARCHIVE_NAME
    write_text(rootfs, "\n".join(
        "# %s is not set" % option
        for option in ("CONFIG_tcf-agent", "CONFIG_tcf-agent-dbg", "CONFIG_tcf-agent-dev")
    ) + "\n")
    write_json(inventory, {"recipes": [], "source_packages": []})
    write_json(manifest, {"files": []})
    return rootfs, inventory, manifest, archive


def expect_failure(verifier, mutate, marker: str) -> None:
    with tempfile.TemporaryDirectory(prefix="v5-tcf-rootfs-") as raw_root:
        paths = make_fixture(Path(raw_root), verifier)
        mutate(*paths)
        failures = verifier.audit_tcf_production_closure(*paths)
        assert marker in failures, (marker, failures)


def main() -> int:
    verifier = load_verifier()
    archive_path = MANIFEST_PATH.parent / verifier.TCF_ARCHIVE_NAME
    assert verifier.audit_tcf_production_closure(
        ROOTFS_PATH, INVENTORY_PATH, MANIFEST_PATH, archive_path) == []
    text = ROOTFS_PATH.read_text(encoding="utf-8")
    for option in ("CONFIG_tcf-agent", "CONFIG_tcf-agent-dbg", "CONFIG_tcf-agent-dev"):
        disabled = "# %s is not set" % option
        assert disabled in text
        expect_failure(
            verifier,
            lambda rootfs, _inventory, _manifest, _archive, option=option: write_text(
                rootfs, text.replace(disabled, "%s=y" % option, 1)),
            "TCF_ROOTFS_ACTIVE_ASSIGNMENT:%s" % option,
        )
        expect_failure(
            verifier,
            lambda rootfs, _inventory, _manifest, _archive, option=option: write_text(
                rootfs, text.replace(disabled, "", 1)),
            "TCF_ROOTFS_DISABLED_COUNT:%s:0" % option,
        )
        expect_failure(
            verifier,
            lambda rootfs, _inventory, _manifest, _archive, option=option: write_text(
                rootfs, text + "# %s is not set\n" % option),
            "TCF_ROOTFS_DISABLED_COUNT:%s:2" % option,
        )
    expect_failure(
        verifier,
        lambda _rootfs, inventory, _manifest, _archive: write_json(
            inventory, {"recipes": [{"pn": "tcf-agent", "source_packages": []}], "source_packages": []}),
        "TCF_INVENTORY_RECIPE_PRESENT",
    )
    expect_failure(
        verifier,
        lambda _rootfs, inventory, _manifest, _archive: write_json(
            inventory, {"recipes": [{"pn": "other", "source_packages": [verifier.TCF_ARCHIVE_NAME]}], "source_packages": []}),
        "TCF_INVENTORY_RECIPE_PACKAGE_PRESENT",
    )
    expect_failure(
        verifier,
        lambda _rootfs, inventory, _manifest, _archive: write_json(
            inventory, {"recipes": [], "source_packages": [{"path": verifier.TCF_ARCHIVE_NAME}]}),
        "TCF_INVENTORY_PACKAGE_PRESENT",
    )
    expect_failure(
        verifier,
        lambda _rootfs, inventory, _manifest, _archive: write_json(
            inventory, {"recipes": [], "source_packages": [{"path": "other", "url": "git://git.eclipse.org/gitroot/tcf/org.eclipse.tcf.agent"}]}),
        "TCF_INVENTORY_URL_PRESENT",
    )
    expect_failure(
        verifier,
        lambda _rootfs, _inventory, manifest, _archive: write_json(
            manifest, {"files": [{"path": verifier.TCF_ARCHIVE_NAME}]}),
        "TCF_SOURCE_MANIFEST_ENTRY_PRESENT",
    )
    expect_failure(
        verifier,
        lambda _rootfs, _inventory, _manifest, archive: archive.write_bytes(
            b"x" * verifier.TCF_ARCHIVE_SIZE),
        "TCF_SOURCE_ARCHIVE_PRESENT:%d" % verifier.TCF_ARCHIVE_SIZE,
    )
    with tempfile.TemporaryDirectory(prefix="v5-tcf-clean-") as raw_root:
        paths = make_fixture(Path(raw_root), verifier)
        assert verifier.audit_tcf_production_closure(*paths) == []
    print("V5_PETALINUX_TCF_PRODUCTION_CLOSURE_POLICY_OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
