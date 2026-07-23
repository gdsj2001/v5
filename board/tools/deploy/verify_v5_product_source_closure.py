#!/usr/bin/env python3
"""Fail closed when V5 product sources are not in the build/deploy closure."""

from __future__ import annotations

import argparse
import ast
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
from typing import Dict, Iterable, List, Set, Tuple


KNOWN_KINDS = {
    "binary",
    "script",
    "module",
    "init",
    "linuxcnc",
    "runtime_seed",
    "runtime_seed_merge",
    "runtime_ini_cycle_merge",
    "config",
    "gcode",
    "kernel_module",
}
C_SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx"}
HEADER_SUFFIXES = {".h", ".hh", ".hpp", ".hxx"}
SCANNED_SUFFIXES = C_SOURCE_SUFFIXES | HEADER_SUFFIXES | {".py", ".sh"}
TEST_SUFFIXES = ("_smoke", "_test", "_test_remote")
QUOTED_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.MULTILINE)


class ClosureError(RuntimeError):
    pass


def _relative_source(text: str, line_number: int) -> PurePosixPath:
    source = PurePosixPath(text)
    if source.is_absolute() or not source.parts or ".." in source.parts:
        raise ClosureError(f"manifest line {line_number}: invalid relative source {text!r}")
    return source


def parse_manifest(path: Path, board_root: Path) -> Tuple[List[Dict[str, str]], Set[str]]:
    rows: List[Dict[str, str]] = []
    binary_targets: Set[str] = set()
    seen_sources: Set[str] = set()
    seen_destinations: Set[str] = set()
    for line_number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not raw or raw.startswith("#"):
            continue
        fields = raw.split("\t")
        if len(fields) != 4:
            raise ClosureError(f"manifest line {line_number}: expected 4 tab fields")
        kind, source_text, destination, mode = fields
        if kind not in KNOWN_KINDS:
            raise ClosureError(f"manifest line {line_number}: unknown kind {kind!r}")
        source = _relative_source(source_text, line_number)
        if not destination.startswith("/") or ".." in PurePosixPath(destination).parts:
            raise ClosureError(f"manifest line {line_number}: invalid destination {destination!r}")
        if re.fullmatch(r"0[0-7]{3}", mode) is None:
            raise ClosureError(f"manifest line {line_number}: invalid mode {mode!r}")
        normalized_source = source.as_posix()
        if normalized_source in seen_sources:
            raise ClosureError(f"manifest line {line_number}: duplicate source {normalized_source}")
        if destination in seen_destinations:
            raise ClosureError(f"manifest line {line_number}: duplicate destination {destination}")
        seen_sources.add(normalized_source)
        seen_destinations.add(destination)
        if kind == "binary" and source.parts[:3] == ("build", "board", "app"):
            target = source.name
            if not target or target in binary_targets:
                raise ClosureError(f"manifest line {line_number}: duplicate/empty binary target {target}")
            binary_targets.add(target)
        elif kind == "binary":
            if normalized_source != "build/ethercat/lcec.so":
                raise ClosureError(
                    f"manifest line {line_number}: unregistered external binary {normalized_source}"
                )
        elif kind == "kernel_module":
            if source.parts[:2] != ("build", "ethercat") or source.suffix != ".ko":
                raise ClosureError(
                    f"manifest line {line_number}: invalid kernel module source {normalized_source}"
                )
        elif not (board_root / Path(*source.parts)).is_file():
            raise ClosureError(f"manifest line {line_number}: source is missing: {normalized_source}")
        rows.append(
            {
                "kind": kind,
                "source": normalized_source,
                "destination": destination,
                "mode": mode,
            }
        )
    if not binary_targets:
        raise ClosureError("deploy manifest contains no binary targets")
    return rows, binary_targets


def prepare_file_api_query(build_dir: Path) -> None:
    query_dir = build_dir / ".cmake" / "api" / "v1" / "query"
    query_dir.mkdir(parents=True, exist_ok=True)
    (query_dir / "codemodel-v2").touch()


def _latest_index(reply_dir: Path) -> Path:
    indexes = sorted(reply_dir.glob("index-*.json"), key=lambda item: item.stat().st_mtime_ns)
    if not indexes:
        raise ClosureError(f"CMake File API index is missing under {reply_dir}")
    return indexes[-1]


def load_cmake_c_source_classes(
    board_root: Path, build_dir: Path, binary_targets: Set[str]
) -> Tuple[Set[str], Set[str]]:
    reply_dir = build_dir / ".cmake" / "api" / "v1" / "reply"
    index = json.loads(_latest_index(reply_dir).read_text(encoding="utf-8"))
    codemodel_ref = index.get("reply", {}).get("codemodel-v2")
    if not isinstance(codemodel_ref, dict) or not codemodel_ref.get("jsonFile"):
        raise ClosureError("CMake File API codemodel-v2 reply is missing")
    codemodel = json.loads((reply_dir / codemodel_ref["jsonFile"]).read_text(encoding="utf-8"))
    configurations = codemodel.get("configurations", [])
    if not configurations:
        raise ClosureError("CMake codemodel has no configuration")
    target_refs: Dict[str, Dict[str, object]] = {}
    targets_by_name: Dict[str, str] = {}
    for target_ref in configurations[0].get("targets", []):
        target_id = str(target_ref.get("id", ""))
        json_file = target_ref.get("jsonFile")
        name = str(target_ref.get("name", ""))
        if not target_id or not json_file or not name:
            continue
        target = json.loads((reply_dir / str(json_file)).read_text(encoding="utf-8"))
        target_refs[target_id] = target
        targets_by_name[name] = target_id
    missing_targets = sorted(binary_targets - set(targets_by_name))
    if missing_targets:
        raise ClosureError(f"manifest binary targets are absent from CMake: {missing_targets}")

    board_root_resolved = board_root.resolve()
    cmake_source_root = Path(codemodel.get("paths", {}).get("source", board_root)).resolve()

    def collect(start_ids: Iterable[str]) -> Set[str]:
        pending = list(start_ids)
        visited: Set[str] = set()
        source_paths: Set[str] = set()
        while pending:
            target_id = pending.pop()
            if target_id in visited:
                continue
            visited.add(target_id)
            target = target_refs.get(target_id)
            if not target:
                raise ClosureError(f"CMake target dependency is missing: {target_id}")
            for dependency in target.get("dependencies", []):
                dependency_id = str(dependency.get("id", ""))
                if dependency_id and dependency_id in target_refs:
                    pending.append(dependency_id)
            for source in target.get("sources", []):
                if source.get("isGenerated"):
                    continue
                raw_path = Path(str(source.get("path", "")))
                if not raw_path:
                    continue
                absolute = raw_path if raw_path.is_absolute() else cmake_source_root / raw_path
                try:
                    relative = absolute.resolve().relative_to(board_root_resolved).as_posix()
                except ValueError:
                    continue
                if Path(relative).suffix.lower() in C_SOURCE_SUFFIXES:
                    source_paths.add(relative)
        return source_paths

    runtime_sources = collect(targets_by_name[name] for name in sorted(binary_targets))
    test_target_ids = [
        target_id
        for name, target_id in targets_by_name.items()
        if name.startswith("test_") or name.endswith(TEST_SUFFIXES) or "_smoke_" in name
    ]
    product_prefixes = ("app/", "services/")
    runtime_sources = {
        source for source in runtime_sources if source.startswith(product_prefixes)
    }
    test_sources = {
        source
        for source in (collect(test_target_ids) - runtime_sources)
        if source.startswith(product_prefixes)
    }
    return runtime_sources, test_sources


def is_test_source(path: Path) -> bool:
    lowered_parts = {part.lower() for part in path.parts}
    if lowered_parts.intersection({"test", "tests"}):
        return True
    stem = path.stem.lower()
    return stem.startswith("test_") or stem.endswith(TEST_SUFFIXES) or "_smoke_" in stem


def iter_scanned_sources(board_root: Path) -> Iterable[Path]:
    for relative_root in (
        Path("app/src"),
        Path("app/include"),
        Path("services"),
        Path("tools/v5_touch_calibration"),
    ):
        root = board_root / relative_root
        if not root.is_dir():
            raise ClosureError(f"product source root is missing: {relative_root.as_posix()}")
        for path in sorted(root.rglob("*")):
            if path.is_file() and path.suffix.lower() in SCANNED_SUFFIXES:
                yield path


def collect_reachable_headers(board_root: Path, start_sources: Set[str]) -> Set[str]:
    header_paths = [
        path
        for path in iter_scanned_sources(board_root)
        if path.suffix.lower() in HEADER_SUFFIXES
    ]
    by_name: Dict[str, List[Path]] = {}
    for path in header_paths:
        by_name.setdefault(path.name, []).append(path)
    reachable: Set[str] = set()
    pending = [board_root / Path(source) for source in sorted(start_sources)]
    visited: Set[Path] = set()
    search_roots = (board_root, board_root / "app/include", board_root / "app/src")
    while pending:
        source = pending.pop()
        source = source.resolve()
        if source in visited or not source.is_file():
            continue
        visited.add(source)
        try:
            text = source.read_text(encoding="utf-8", errors="strict")
        except UnicodeError as exc:
            raise ClosureError(f"source encoding failed: {source}: {exc}") from exc
        for token in QUOTED_INCLUDE_RE.findall(text):
            token_path = Path(token)
            candidates = [source.parent / token_path]
            candidates.extend(root / token_path for root in search_roots)
            candidates.extend(by_name.get(token_path.name, []))
            matches: List[Path] = []
            for candidate in candidates:
                if not candidate.is_file():
                    continue
                resolved = candidate.resolve()
                try:
                    relative = resolved.relative_to(board_root.resolve()).as_posix()
                except ValueError:
                    continue
                if relative.endswith(token_path.as_posix()) and resolved not in matches:
                    matches.append(resolved)
            if len(matches) > 1:
                exact = [
                    match
                    for match in matches
                    if match.relative_to(board_root.resolve()).as_posix() == token_path.as_posix()
                ]
                if len(exact) == 1:
                    matches = exact
            if len(matches) != 1:
                continue
            header = matches[0]
            relative = header.relative_to(board_root.resolve()).as_posix()
            if relative not in reachable:
                reachable.add(relative)
                pending.append(header)
    return reachable


def validate_manifest_sources(
    board_root: Path, rows: List[Dict[str, str]], validate_shell: bool
) -> None:
    for row in rows:
        if row["kind"] in {"binary", "kernel_module"}:
            continue
        source = board_root / Path(row["source"])
        suffix = source.suffix.lower()
        if suffix == ".py":
            try:
                ast.parse(source.read_text(encoding="utf-8"), filename=str(source))
            except (SyntaxError, UnicodeError) as exc:
                raise ClosureError(f"Python syntax failed: {row['source']}: {exc}") from exc
        elif suffix == ".json":
            try:
                json.loads(source.read_text(encoding="utf-8"))
            except (json.JSONDecodeError, UnicodeError) as exc:
                raise ClosureError(f"JSON syntax failed: {row['source']}: {exc}") from exc
        elif suffix == ".tsv":
            text = source.read_text(encoding="utf-8")
            if "\x00" in text or not text.strip():
                raise ClosureError(f"TSV is empty or contains NUL: {row['source']}")
        if validate_shell and (suffix == ".sh" or row["kind"] == "init"):
            shell = shutil.which("sh")
            if shell is None:
                raise ClosureError("shell syntax checker is unavailable: sh")
            result = subprocess.run(
                [shell, "-n", str(source)], capture_output=True, text=True, check=False
            )
            if result.returncode != 0:
                raise ClosureError(
                    f"shell syntax failed: {row['source']}: {result.stderr.strip()}"
                )


def verify_closure(
    board_root: Path, build_dir: Path, manifest: Path, validate_shell: bool
) -> Tuple[int, int, int]:
    rows, binary_targets = parse_manifest(manifest, board_root)
    runtime_c_sources, cmake_test_sources = load_cmake_c_source_classes(
        board_root, build_dir, binary_targets
    )
    runtime_headers = collect_reachable_headers(board_root, runtime_c_sources)
    test_headers = collect_reachable_headers(board_root, cmake_test_sources) - runtime_headers
    manifest_sources = {
        row["source"]
        for row in rows
        if row["kind"] not in {"binary", "kernel_module"}
    }
    unclassified: List[str] = []
    test_count = 0
    scanned_count = 0
    for source in iter_scanned_sources(board_root):
        scanned_count += 1
        relative = source.relative_to(board_root).as_posix()
        if source.suffix.lower() in C_SOURCE_SUFFIXES and relative in runtime_c_sources:
            continue
        if source.suffix.lower() in C_SOURCE_SUFFIXES and relative in cmake_test_sources:
            test_count += 1
            continue
        if source.suffix.lower() in HEADER_SUFFIXES and relative in runtime_headers:
            continue
        if source.suffix.lower() in HEADER_SUFFIXES and relative in test_headers:
            test_count += 1
            continue
        if relative in manifest_sources:
            continue
        if is_test_source(source.relative_to(board_root)):
            test_count += 1
            if source.suffix.lower() == ".py":
                ast.parse(source.read_text(encoding="utf-8"), filename=str(source))
            continue
        unclassified.append(relative)
    if unclassified:
        raise ClosureError(
            "unclassified production sources are outside product build/deploy closure: "
            + ", ".join(sorted(unclassified))
        )
    validate_manifest_sources(board_root, rows, validate_shell)
    return scanned_count, len(runtime_c_sources), test_count


def self_test() -> None:
    assert is_test_source(Path("services/demo/example_smoke.py"))
    assert is_test_source(Path("app/src/test_example.c"))
    assert not is_test_source(Path("services/demo/example.py"))
    assert _relative_source("services/demo/example.py", 1).as_posix() == "services/demo/example.py"
    try:
        _relative_source("../escape.py", 1)
    except ClosureError:
        pass
    else:
        raise AssertionError("relative path escape was accepted")
    print("V5_PRODUCT_SOURCE_CLOSURE_SELF_TEST_OK")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--board-root", type=Path)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--prepare-cmake-query", action="store_true")
    parser.add_argument("--validate-shell", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.board_root is None or args.build_dir is None:
        parser.error("--board-root and --build-dir are required")
    board_root = args.board_root.resolve()
    build_dir = args.build_dir.resolve()
    if args.prepare_cmake_query:
        prepare_file_api_query(build_dir)
        print(f"V5_PRODUCT_SOURCE_CLOSURE_QUERY_READY build_dir={build_dir}")
        return 0
    manifest = (
        args.manifest.resolve()
        if args.manifest is not None
        else board_root / "config/deploy/v5_runtime_deploy_manifest.tsv"
    )
    try:
        scanned, runtime_c, tests = verify_closure(
            board_root, build_dir, manifest, args.validate_shell
        )
    except (ClosureError, OSError, ValueError, json.JSONDecodeError, SyntaxError) as exc:
        print(f"V5_PRODUCT_SOURCE_CLOSURE_FAILED: {exc}", file=sys.stderr)
        return 1
    print(
        "V5_PRODUCT_SOURCE_CLOSURE_OK "
        f"scanned={scanned} runtime_c={runtime_c} tests={tests}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
