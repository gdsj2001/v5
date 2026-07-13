#!/usr/bin/env python3
"""Export the remote source inputs used by the active BitBake task graph."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import bb.fetch2
import bb.tinfoil


SCHEMA = "v5-bitbake-source-inventory-v1"
REMOTE_SCHEMES = {
    "az",
    "bzr",
    "cvs",
    "ftp",
    "git",
    "gitsm",
    "hg",
    "http",
    "https",
    "npm",
    "osc",
    "p4",
    "repo",
    "s3",
    "svn",
}


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--pn-buildlist", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args()


def load_pns(path):
    return sorted(
        {
            line.strip()
            for line in path.read_text(encoding="utf-8").splitlines()
            if line.strip()
        }
    )


def source_package_names(ud):
    names = list(getattr(ud, "mirrortarballs", []) or [])
    if names:
        return names
    local_file = getattr(ud, "localfile", "") or ""
    if not local_file:
        return []
    return [Path(local_file).name]


def checksum_fields(datastore, name):
    fields = {}
    for checksum_name in ("md5sum", "sha256sum"):
        value = datastore.getVarFlag("SRC_URI", "%s.%s" % (name, checksum_name))
        if not value and name == "default":
            value = datastore.getVarFlag("SRC_URI", checksum_name)
        if value:
            fields[checksum_name] = value
    return fields


def main():
    args = parse_args()
    pns = load_pns(args.pn_buildlist)
    packages = {}
    recipes = []

    with bb.tinfoil.Tinfoil() as tinfoil:
        tinfoil.prepare(quiet=2)
        for pn in pns:
            datastore = tinfoil.parse_recipe(pn)
            src_uri = datastore.getVar("SRC_URI") or ""
            datastore.setVar("BB_GENERATE_MIRROR_TARBALLS", "1")
            fetcher = bb.fetch2.Fetch(src_uri.split(), datastore)
            recipe_record = {
                "pn": datastore.getVar("PN") or pn,
                "pv": datastore.getVar("PV") or "",
                "license": datastore.getVar("LICENSE") or "",
                "lic_files_chksum": datastore.getVar("LIC_FILES_CHKSUM") or "",
                "srcrev": datastore.getVar("SRCREV") or "",
                "source_packages": [],
            }
            for url in fetcher.urls:
                scheme = url.split(":", 1)[0].lower()
                if scheme not in REMOTE_SCHEMES:
                    continue
                ud = fetcher.ud[url]
                names = source_package_names(ud)
                uri_name = getattr(ud, "names", ["default"])[0]
                checksums = checksum_fields(datastore, uri_name)
                for package_name in names:
                    if not package_name or "/" in package_name or "\\" in package_name:
                        raise RuntimeError(
                            "invalid source package name for %s: %s" % (pn, package_name)
                        )
                    record = {
                        "path": package_name,
                        "scheme": scheme,
                        "url": url,
                        "checksums": checksums,
                        "recipes": [],
                    }
                    existing = packages.setdefault(package_name, record)
                    if existing["url"] != url and url not in existing.setdefault("alternate_urls", []):
                        existing["alternate_urls"].append(url)
                    recipe_key = "%s@%s" % (recipe_record["pn"], recipe_record["pv"])
                    if recipe_key not in existing["recipes"]:
                        existing["recipes"].append(recipe_key)
                    if package_name not in recipe_record["source_packages"]:
                        recipe_record["source_packages"].append(package_name)
            recipes.append(recipe_record)

    payload = {
        "schema": SCHEMA,
        "target": "petalinux-image-minimal",
        "recipe_count": len(recipes),
        "source_package_count": len(packages),
        "recipes": recipes,
        "source_packages": [packages[name] for name in sorted(packages)],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(payload, indent=2, ensure_ascii=True) + "\n",
        encoding="utf-8",
    )
    print(
        "V5_SOURCE_INVENTORY_OK recipes=%d source_packages=%d"
        % (payload["recipe_count"], payload["source_package_count"])
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
