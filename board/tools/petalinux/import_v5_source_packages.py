#!/usr/bin/env python3
"""Import BitBake source packages directly on the Windows source owner."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
import urllib.parse
from pathlib import Path


INVENTORY_SCHEMA = "v5-bitbake-source-inventory-v1"
MISSING_SCHEMA = "v5-missing-source-inputs-v1"
DEFAULT_INVENTORY = Path("board/petalinux/v5_bitbake_source_inventory.json")
DEFAULT_DESTINATION = Path("board/third_party/petalinux-source-packages")
DEFAULT_VERIFIER = Path("board/tools/petalinux/verify_v5_source_packages.py")
GIT_SCHEMES = {"git", "gitsm"}


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-root", type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument("--inventory", type=Path)
    parser.add_argument("--destination", type=Path)
    parser.add_argument("--missing-report", type=Path)
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args()


def load_inventory(path):
    payload = json.loads(path.read_text(encoding="utf-8"))
    if payload.get("schema") != INVENTORY_SCHEMA:
        raise RuntimeError("unexpected BitBake source inventory schema")
    recipe_licenses = {
        "%s@%s" % (recipe.get("pn", ""), recipe.get("pv", "")): recipe.get("license", "")
        for recipe in payload.get("recipes", [])
    }
    names = set()
    for record in payload.get("source_packages", []):
        name = str(record.get("path", ""))
        if not name or name != Path(name).name or "/" in name or "\\" in name:
            raise RuntimeError("invalid inventory source package path: %r" % name)
        if name in names:
            raise RuntimeError("duplicate inventory source package path: %s" % name)
        names.add(name)
        scheme = str(record.get("scheme", ""))
        if scheme not in {"http", "https", "ftp", "git", "gitsm"}:
            raise RuntimeError("unsupported source package scheme: %s" % scheme)
        recipes = list(record.get("recipes", []))
        if not recipes or any(not recipe_licenses.get(recipe) for recipe in recipes):
            raise RuntimeError("source package has no licensed recipe owner: %s" % name)
        expected = str(record.get("checksums", {}).get("sha256sum", ""))
        if scheme not in GIT_SCHEMES and (
            len(expected) != 64
            or any(character not in "0123456789abcdef" for character in expected.lower())
        ):
            raise RuntimeError("source package has no fixed SHA-256: %s" % name)
    if len(names) != int(payload.get("source_package_count", -1)):
        raise RuntimeError("source package inventory count does not match its records")
    return payload


def load_missing_report(path, inventory_path):
    report = json.loads(path.read_text(encoding="utf-8"))
    if report.get("schema") != MISSING_SCHEMA:
        raise RuntimeError("unexpected missing source input report schema")
    if report.get("action") != "windows_import":
        raise RuntimeError("missing source input report is not safe for automatic import")
    if report.get("extra"):
        raise RuntimeError("automatic import cannot remove extra canonical source files")
    actual_inventory_hash = sha256_file(inventory_path)
    if report.get("inventory_sha256") != actual_inventory_hash:
        raise RuntimeError("missing report inventory identity is stale")
    return report


def selected_records(inventory, report):
    records = {record["path"]: record for record in inventory["source_packages"]}
    if report is None:
        return list(inventory["source_packages"]), "inventory-full"
    requested = []
    report_items = {}
    for key in ("missing", "invalid"):
        for item in report.get(key, []):
            name = item.get("path", "")
            if name not in records:
                raise RuntimeError(f"missing report references an unregistered package: {name}")
            if name not in requested:
                requested.append(name)
            report_items[name] = (key, item)
    if not requested:
        raise RuntimeError("missing report contains no importable source packages")
    selected = []
    for name in requested:
        key, item = report_items[name]
        record = dict(records[name])
        record["_force_replace"] = key == "invalid"
        record["_report_sha256"] = item.get("expected_sha256", "")
        selected.append(record)
    return selected, str(report.get("resume_scope", ""))


def expected_revisions(inventory):
    revisions = {}
    for recipe in inventory["recipes"]:
        revision = recipe.get("srcrev", "")
        if len(revision) != 40 or any(char not in "0123456789abcdef" for char in revision.lower()):
            continue
        for package in recipe.get("source_packages", []):
            revisions.setdefault(package, set()).add(revision)
    return revisions


def candidate_urls(record):
    name = urllib.parse.quote(record["path"])
    candidates = [
        "https://edf.amd.com/sswreleases/rel-v2020/downloads/%s" % name,
        "https://downloads.yoctoproject.org/mirror/sources/%s" % name,
        "https://sources.openembedded.org/%s" % name,
    ]
    urls = [record["url"]] + list(record.get("alternate_urls", []))
    for url in urls:
        base = url.split(";", 1)[0]
        if base.startswith(("http://", "https://", "ftp://")) and base not in candidates:
            candidates.append(base)
    return candidates


def download(candidate, target, timeout):
    result = subprocess.run(
        [
            "curl.exe",
            "--location",
            "--fail",
            "--silent",
            "--show-error",
            "--retry",
            "4",
            "--retry-delay",
            "2",
            "--retry-all-errors",
            "--continue-at",
            "-",
            "--connect-timeout",
            "15",
            "--speed-limit",
            "65536",
            "--speed-time",
            "30",
            "--max-time",
            str(timeout),
            "--user-agent",
            "v5-source-import/1",
            "--output",
            str(target),
            candidate,
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or "curl failed")


def expected_sha256(record):
    if record["scheme"] in GIT_SCHEMES:
        return ""
    return record.get("checksums", {}).get("sha256sum", "")


def verify_package(path, record):
    actual = sha256_file(path)
    expected = record.get("_report_sha256", "") or expected_sha256(record)
    if expected and actual != expected:
        raise RuntimeError(
            "source package checksum mismatch: %s != %s" % (actual, expected)
        )
    return actual


def git_clone_url(url):
    parts = url.split(";")
    base = parts[0]
    options = {}
    for option in parts[1:]:
        if "=" in option:
            key, value = option.split("=", 1)
            options[key] = value
    scheme, remainder = base.split("://", 1)
    protocol = options.get("protocol", "https" if scheme in GIT_SCHEMES else scheme)
    return "%s://%s" % (protocol, remainder)


def build_git_package(record, revisions, target):
    clone_url = git_clone_url(record["url"])
    with tempfile.TemporaryDirectory(prefix="v5-source-import-") as temporary:
        repository = Path(temporary) / "repository"
        subprocess.run(
            ["git", "clone", "--mirror", clone_url, str(repository)],
            check=True,
        )
        for revision in sorted(revisions):
            subprocess.run(
                ["git", "-C", str(repository), "cat-file", "-e", "%s^{commit}" % revision],
                check=True,
            )
        subprocess.run(
            ["tar", "-czf", str(target), "-C", str(repository), "."],
            check=True,
        )


def import_package(record, revisions, destination, timeout):
    target = destination / record["path"]
    if target.is_file() and not record.get("_force_replace", False):
        try:
            return "existing", verify_package(target, record)
        except RuntimeError:
            pass
    partial = target.with_name(target.name + ".part")
    failures = []
    for candidate in candidate_urls(record):
        try:
            download(candidate, partial, timeout)
            digest = verify_package(partial, record)
            partial.replace(target)
            return candidate, digest
        except (OSError, RuntimeError) as exc:
            partial.unlink(missing_ok=True)
            failures.append("%s: %s" % (candidate, exc))
    if record["scheme"] in GIT_SCHEMES:
        try:
            build_git_package(record, revisions, partial)
            digest = verify_package(partial, record)
            partial.replace(target)
            return "windows-git:%s" % git_clone_url(record["url"]), digest
        except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
            partial.unlink(missing_ok=True)
            failures.append("windows-git: %s" % exc)
    raise RuntimeError("; ".join(failures))


def rebuild_manifest(project_root):
    verifier = project_root / DEFAULT_VERIFIER
    subprocess.run(
        [
            sys.executable,
            str(verifier),
            "--project-root",
            str(project_root),
            "--write-manifest",
        ],
        check=True,
    )


def self_test():
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        inventory_path = root / "inventory.json"
        inventory = {
            "schema": INVENTORY_SCHEMA,
            "source_package_count": 1,
            "recipes": [{"pn": "example", "pv": "1", "license": "MIT"}],
            "source_packages": [
                {
                    "path": "example.tar.gz",
                    "scheme": "https",
                    "url": "https://example.invalid/example.tar.gz",
                    "checksums": {"sha256sum": "0" * 64},
                    "recipes": ["example@1"],
                }
            ],
        }
        inventory_path.write_text(json.dumps(inventory), encoding="utf-8")
        inventory = load_inventory(inventory_path)
        report = {
            "schema": MISSING_SCHEMA,
            "action": "windows_import",
            "resume_scope": "example-focused",
            "inventory_sha256": sha256_file(inventory_path),
            "missing": [{"path": "example.tar.gz"}],
            "invalid": [],
            "extra": [],
        }
        report_path = root / "missing.json"
        report_path.write_text(json.dumps(report), encoding="utf-8")
        loaded = load_missing_report(report_path, inventory_path)
        records, scope = selected_records(inventory, loaded)
        assert [record["path"] for record in records] == ["example.tar.gz"]
        assert not records[0]["_force_replace"]
        assert scope == "example-focused"
        report["missing"] = []
        report["invalid"] = [
            {"path": "example.tar.gz", "expected_sha256": "1" * 64}
        ]
        report_path.write_text(json.dumps(report), encoding="utf-8")
        records, _ = selected_records(
            inventory, load_missing_report(report_path, inventory_path)
        )
        assert records[0]["_force_replace"]
        assert records[0]["_report_sha256"] == "1" * 64
        report["inventory_sha256"] = "f" * 64
        report_path.write_text(json.dumps(report), encoding="utf-8")
        try:
            load_missing_report(report_path, inventory_path)
        except RuntimeError:
            pass
        else:
            raise AssertionError("stale missing report inventory was accepted")
    print("V5_WINDOWS_SOURCE_IMPORT_SELF_TEST_OK")


def main():
    args = parse_args()
    if args.self_test:
        self_test()
        return 0
    project_root = args.project_root.resolve()
    inventory_path = (
        args.inventory.resolve()
        if args.inventory
        else (project_root / DEFAULT_INVENTORY).resolve()
    )
    destination = (
        args.destination.resolve()
        if args.destination
        else (project_root / DEFAULT_DESTINATION).resolve()
    )
    if project_root not in destination.parents:
        raise RuntimeError("source package destination escaped project root")
    inventory = load_inventory(inventory_path)
    report = (
        load_missing_report(args.missing_report.resolve(), inventory_path)
        if args.missing_report
        else None
    )
    records, resume_scope = selected_records(inventory, report)
    revisions = expected_revisions(inventory)
    destination.mkdir(parents=True, exist_ok=True)
    failures = []
    imported = 0
    for index, record in enumerate(records, 1):
        try:
            source, digest = import_package(
                record,
                revisions.get(record["path"], set()),
                destination,
                args.timeout,
            )
            imported += 1
            print(
                "[%d/%d] %s sha256=%s source=%s"
                % (index, len(records), record["path"], digest, source),
                flush=True,
            )
        except RuntimeError as exc:
            failures.append("%s: %s" % (record["path"], exc))
            print("[%d/%d] FAILED %s" % (index, len(records), record["path"]), flush=True)
    if failures:
        raise RuntimeError("source import failures:\n" + "\n".join(failures))
    rebuild_manifest(project_root)
    print(
        "V5_WINDOWS_SOURCE_IMPORT_OK packages=%d resume_scope=%s"
        % (imported, resume_scope)
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
