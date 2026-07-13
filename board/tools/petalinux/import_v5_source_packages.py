#!/usr/bin/env python3
"""Import BitBake source packages directly on the Windows source owner."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import tempfile
import urllib.parse
from pathlib import Path


INVENTORY_SCHEMA = "v5-bitbake-source-inventory-v1"
DEFAULT_INVENTORY = Path("board/petalinux/v5_bitbake_source_inventory.json")
DEFAULT_DESTINATION = Path("board/third_party/petalinux-source-packages")
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
    parser.add_argument("--timeout", type=int, default=600)
    return parser.parse_args()


def load_inventory(path):
    payload = json.loads(path.read_text(encoding="utf-8"))
    if payload.get("schema") != INVENTORY_SCHEMA:
        raise RuntimeError("unexpected BitBake source inventory schema")
    return payload


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
    expected = expected_sha256(record)
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
    if target.is_file():
        return "existing", verify_package(target, record)
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


def main():
    args = parse_args()
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
    revisions = expected_revisions(inventory)
    destination.mkdir(parents=True, exist_ok=True)
    failures = []
    imported = 0
    for index, record in enumerate(inventory["source_packages"], 1):
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
                % (index, inventory["source_package_count"], record["path"], digest, source),
                flush=True,
            )
        except RuntimeError as exc:
            failures.append("%s: %s" % (record["path"], exc))
            print("[%d/%d] FAILED %s" % (index, inventory["source_package_count"], record["path"]), flush=True)
    if failures:
        raise RuntimeError("source import failures:\n" + "\n".join(failures))
    print("V5_WINDOWS_SOURCE_IMPORT_OK packages=%d" % imported)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
