#!/usr/bin/env python3
"""Verify the Windows-owned PetaLinux source package closure."""

from __future__ import annotations

import argparse
import hashlib
import json
import tempfile
from pathlib import Path


SCHEMA = "v5-petalinux-source-packages-v1"
MISSING_SCHEMA = "v5-missing-source-inputs-v1"
INVENTORY_SCHEMA = "v5-bitbake-source-inventory-v1"
DEFAULT_SOURCE_ROOT = Path("board/third_party/petalinux-source-packages")
DEFAULT_INVENTORY = Path("board/petalinux/v5_bitbake_source_inventory.json")
MANIFEST_NAME = "v5_source_packages.json"
FORBIDDEN_NAMES = {".git", ".gitattributes", ".gitignore", ".mailmap"}
GIT_SCHEMES = {"git", "gitsm"}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def payload_entries(source_root: Path) -> list[dict[str, object]]:
    if not source_root.exists():
        return []
    if not source_root.is_dir():
        raise RuntimeError(f"source package root is not a directory: {source_root}")

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
        if path.name.endswith((".lock", ".bad-checksum", ".part")):
            raise RuntimeError(f"transient download state is forbidden: {path}")
        entries.append(
            {
                "path": path.name,
                "size": path.stat().st_size,
                "sha256": sha256_file(path),
            }
        )
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
    if payload.get("schema") != INVENTORY_SCHEMA:
        raise RuntimeError("unexpected BitBake source inventory schema")
    return payload, sha256_file(path)


def inventory_records(inventory: dict[str, object]) -> dict[str, dict[str, object]]:
    recipe_licenses = {
        f"{recipe.get('pn', '')}@{recipe.get('pv', '')}": recipe.get("license", "")
        for recipe in inventory.get("recipes", [])
    }
    records: dict[str, dict[str, object]] = {}
    for record in inventory.get("source_packages", []):
        name = str(record.get("path", ""))
        if not name or name != Path(name).name or "/" in name or "\\" in name:
            raise RuntimeError(f"invalid inventory source package path: {name!r}")
        if name in records:
            raise RuntimeError(f"duplicate inventory source package path: {name}")
        recipes = list(record.get("recipes", []))
        if not recipes or any(not recipe_licenses.get(recipe) for recipe in recipes):
            raise RuntimeError(f"source package has no licensed recipe owner: {name}")
        scheme = str(record.get("scheme", ""))
        expected = str(record.get("checksums", {}).get("sha256sum", ""))
        if scheme not in GIT_SCHEMES and (
            len(expected) != 64
            or any(character not in "0123456789abcdef" for character in expected.lower())
        ):
            raise RuntimeError(f"source package has no fixed SHA-256: {name}")
        records[name] = record
    if len(records) != int(inventory.get("source_package_count", -1)):
        raise RuntimeError("source package inventory count does not match its records")
    return records


def manifest_hashes(source_root: Path) -> dict[str, str]:
    path = source_root / MANIFEST_NAME
    if not path.is_file():
        return {}
    payload = json.loads(path.read_text(encoding="utf-8"))
    if payload.get("schema") != SCHEMA:
        raise RuntimeError("unexpected source package manifest schema")
    return {str(entry["path"]): str(entry["sha256"]) for entry in payload.get("files", [])}


def package_summary(
    record: dict[str, object], expected_sha256: str = ""
) -> dict[str, object]:
    return {
        "path": record["path"],
        "scheme": record.get("scheme", ""),
        "recipes": list(record.get("recipes", [])),
        "expected_sha256": expected_sha256,
    }


def expected_package_hash(
    record: dict[str, object], prior_hashes: dict[str, str]
) -> str:
    if str(record.get("scheme", "")) in GIT_SCHEMES:
        return prior_hashes.get(str(record["path"]), "")
    return str(record.get("checksums", {}).get("sha256sum", ""))


def analyse_closure(
    project_root: Path, source_root: Path, resume_scope: str = "source-package-preflight"
) -> dict[str, object]:
    inventory, inventory_hash = load_inventory(project_root)
    records = inventory_records(inventory)
    entries = payload_entries(source_root)
    actual = {str(entry["path"]): entry for entry in entries}
    expected_names = set(records)
    actual_names = set(actual)
    prior_hashes = manifest_hashes(source_root)

    missing = [
        package_summary(records[name], expected_package_hash(records[name], prior_hashes))
        for name in sorted(expected_names - actual_names)
    ]
    extra = [actual[name] for name in sorted(actual_names - expected_names)]
    invalid: list[dict[str, object]] = []
    for name in sorted(expected_names & actual_names):
        record = records[name]
        expected_hash = expected_package_hash(record, prior_hashes)
        actual_hash = str(actual[name]["sha256"])
        if expected_hash and actual_hash != expected_hash:
            item = package_summary(record, expected_hash)
            item["actual_sha256"] = actual_hash
            invalid.append(item)

    action = "none"
    if extra:
        action = "manual_cleanup"
    elif missing or invalid:
        action = "windows_import"
    return {
        "schema": MISSING_SCHEMA,
        "action": action,
        "resume_scope": resume_scope,
        "inventory_path": DEFAULT_INVENTORY.as_posix(),
        "inventory_sha256": inventory_hash,
        "source_owner": DEFAULT_SOURCE_ROOT.as_posix(),
        "missing": missing,
        "invalid": invalid,
        "extra": extra,
    }


def closure_error(report: dict[str, object]) -> str:
    return (
        "source package closure mismatch: "
        f"missing={[item['path'] for item in report['missing']]} "
        f"invalid={[item['path'] for item in report['invalid']]} "
        f"extra={[item['path'] for item in report['extra']]}"
    )


def make_manifest(project_root: Path, source_root: Path) -> dict[str, object]:
    report = analyse_closure(project_root, source_root)
    if report["action"] != "none":
        raise RuntimeError(closure_error(report))
    entries = payload_entries(source_root)
    inventory, inventory_hash = load_inventory(project_root)
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


def self_test() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        inventory_path = root / DEFAULT_INVENTORY
        source_root = root / DEFAULT_SOURCE_ROOT
        inventory_path.parent.mkdir(parents=True)
        content = b"registered-source\n"
        digest = hashlib.sha256(content).hexdigest()
        inventory = {
            "schema": INVENTORY_SCHEMA,
            "target": "test",
            "recipe_count": 1,
            "source_package_count": 1,
            "recipes": [
                {"pn": "example", "pv": "1", "license": "MIT", "source_packages": ["example.tar.gz"]}
            ],
            "source_packages": [
                {
                    "path": "example.tar.gz",
                    "scheme": "https",
                    "url": "https://example.invalid/example.tar.gz",
                    "checksums": {"sha256sum": digest},
                    "recipes": ["example@1"],
                }
            ],
        }
        inventory_path.write_text(json.dumps(inventory), encoding="utf-8")
        report = analyse_closure(root, source_root, "example-focused")
        assert report["action"] == "windows_import"
        assert report["missing"][0]["path"] == "example.tar.gz"
        assert report["resume_scope"] == "example-focused"
        source_root.mkdir(parents=True)
        (source_root / "example.tar.gz").write_bytes(content)
        write_manifest(root, source_root)
        assert verify_manifest(root, source_root)["package_count"] == 1
    print("V5_SOURCE_PACKAGES_SELF_TEST_OK")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-root", type=Path, default=Path.cwd())
    parser.add_argument("--source-root", type=Path)
    parser.add_argument("--write-manifest", action="store_true")
    parser.add_argument("--missing-report", type=Path)
    parser.add_argument("--resume-scope", default="source-package-preflight")
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.self_test:
        self_test()
        return 0
    project_root = args.project_root.resolve()
    source_root = (
        args.source_root.resolve()
        if args.source_root
        else (project_root / DEFAULT_SOURCE_ROOT).resolve()
    )
    if project_root not in source_root.parents:
        raise RuntimeError(f"source package root escaped project root: {source_root}")

    if args.missing_report:
        report_path = args.missing_report.resolve()
        repo_ignored = project_root / "repo_ignored"
        if project_root in report_path.parents and repo_ignored not in report_path.parents:
            raise RuntimeError("missing report must stay outside source or under repo_ignored")
        report = analyse_closure(project_root, source_root, args.resume_scope)
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(
            json.dumps(report, indent=2, ensure_ascii=True) + "\n", encoding="utf-8"
        )
        if report["action"] != "none":
            print(
                "V5_WINDOWS_SOURCE_IMPORT_REQUIRED "
                f"action={report['action']} report={report_path} "
                f"resume_scope={report['resume_scope']}"
            )
            if report["action"] == "windows_import":
                return 42
            raise RuntimeError(closure_error(report))

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
