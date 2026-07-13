#!/usr/bin/env python3
"""Validate V5 Markdown routing without generating another requirement source."""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, Iterable, List, Sequence, Set, Tuple


REQUIRED_CARD_FIELDS: Tuple[str, ...] = (
    "owner_reqs",
    "read_when",
    "truth",
    "forbidden",
    "readback",
    "impact",
    "acceptance",
    "detail_sections",
)
CARD_BEGIN = "<!-- AI_FAST_READ_BEGIN -->"
CARD_END = "<!-- AI_FAST_READ_END -->"
REQ_ROW_RE = re.compile(r"^\|\s*(REQ-[A-Z0-9-]+)\s*\|")
OWNER_PATH_RE = re.compile(r"`([^`]+\.md)`")
HEADING_RE = re.compile(r"^#{1,6}\s+(.+?)\s*$")
ANCHOR_RE = re.compile(r'^\s*<a\s+id=["\']([^"\']+)["\']\s*></a>\s*$')
FIELD_RE = re.compile(r"^([a-z_]+):\s*\[(.*)]\s*$")


@dataclass
class Report:
    errors: List[str] = field(default_factory=list)
    warnings: List[str] = field(default_factory=list)

    def error(self, message: str) -> None:
        self.errors.append(message)

    def warn(self, message: str) -> None:
        self.warnings.append(message)


@dataclass
class Document:
    path: Path
    lines: List[str]
    visible_lines: List[Tuple[int, str]]
    headings: Set[str]
    anchors: Set[str]
    card: Dict[str, List[str]]


def _project_root() -> Path:
    return Path(__file__).resolve().parents[3]


def _visible_markdown_lines(lines: Sequence[str]) -> List[Tuple[int, str]]:
    visible: List[Tuple[int, str]] = []
    fence_marker: str | None = None
    for number, line in enumerate(lines, 1):
        stripped = line.lstrip()
        marker = stripped[:3] if stripped.startswith(("```", "~~~")) else ""
        if marker:
            if fence_marker is None:
                fence_marker = marker
            elif marker == fence_marker:
                fence_marker = None
            continue
        if fence_marker is None:
            visible.append((number, line))
    return visible


def _parse_list(value: str) -> List[str]:
    if not value.strip():
        return []
    return [item.strip() for item in value.split(",") if item.strip()]


def _normalise_heading(value: str) -> str:
    value = re.sub(r"\s+#+\s*$", "", value.strip())
    return re.sub(r"\s+", " ", value)


def _read_document(path: Path, report: Report) -> Document:
    text = path.read_text(encoding="utf-8-sig")
    lines = text.splitlines()
    visible = _visible_markdown_lines(lines)
    begins = [(number, line) for number, line in visible if line.strip() == CARD_BEGIN]
    ends = [(number, line) for number, line in visible if line.strip() == CARD_END]
    card: Dict[str, List[str]] = {}

    if len(begins) != 1 or len(ends) != 1:
        report.error(
            f"{path}: expected one active fast-read card outside fenced code; "
            f"found begin={len(begins)} end={len(ends)}"
        )
    elif begins[0][0] >= ends[0][0]:
        report.error(f"{path}: fast-read END precedes BEGIN")
    else:
        start, end = begins[0][0], ends[0][0]
        seen: Set[str] = set()
        for number, line in visible:
            if number <= start or number >= end or not line.strip():
                continue
            match = FIELD_RE.match(line.strip())
            if not match:
                report.error(f"{path}:{number}: invalid fast-read card line: {line!r}")
                continue
            key, raw_value = match.groups()
            if key in seen:
                report.error(f"{path}:{number}: duplicate fast-read field {key}")
                continue
            seen.add(key)
            card[key] = _parse_list(raw_value)
        missing = [key for key in REQUIRED_CARD_FIELDS if key not in card]
        extra = sorted(set(card) - set(REQUIRED_CARD_FIELDS))
        if missing:
            report.error(f"{path}: missing fast-read fields: {', '.join(missing)}")
        if extra:
            report.error(f"{path}: unknown fast-read fields: {', '.join(extra)}")

    headings: Set[str] = set()
    anchors: Set[str] = set()
    for _, line in visible:
        heading_match = HEADING_RE.match(line)
        if heading_match:
            headings.add(_normalise_heading(heading_match.group(1)))
        anchor_match = ANCHOR_RE.match(line)
        if anchor_match:
            anchor = anchor_match.group(1)
            if anchor in anchors:
                report.error(f"{path}: duplicate explicit anchor #{anchor}")
            anchors.add(anchor)
    return Document(path, lines, visible, headings, anchors, card)


def _parse_owner_table(index: Document, root: Path, report: Report) -> Dict[str, Path]:
    owners: Dict[str, Path] = {}
    in_owner_table = False
    for number, line in index.visible_lines:
        if line.startswith("## P0 当前需求 owner 表"):
            in_owner_table = True
            continue
        if in_owner_table and line.startswith("## "):
            break
        if not in_owner_table:
            continue
        req_match = REQ_ROW_RE.match(line)
        if not req_match:
            continue
        req = req_match.group(1)
        if req in owners:
            report.error(f"{index.path}:{number}: duplicate owner row for {req}")
            continue
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        owner_cell = cells[2] if len(cells) >= 3 else ""
        path_matches = OWNER_PATH_RE.findall(owner_cell)
        if len(path_matches) != 1:
            report.error(
                f"{index.path}:{number}: {req} must reference exactly one Markdown owner path"
            )
            continue
        owner = root / Path(path_matches[0].replace("/", "\\"))
        owners[req] = owner
        if not owner.is_file():
            report.error(f"{index.path}:{number}: owner for {req} does not exist: {owner}")
    if not owners:
        report.error(f"{index.path}: no REQ owner rows found")
    return owners


def _check_card_ownership(
    docs: Dict[Path, Document], owners: Dict[str, Path], report: Report
) -> None:
    expected: Dict[Path, Set[str]] = {}
    for req, owner in owners.items():
        expected.setdefault(owner.resolve(), set()).add(req)
    for path, document in docs.items():
        actual = set(document.card.get("owner_reqs", []))
        wanted = expected.get(path.resolve(), set())
        unknown = sorted(actual - set(owners))
        if unknown:
            report.error(f"{path}: owner_reqs contains unknown REQ: {', '.join(unknown)}")
        if actual != wanted:
            report.error(
                f"{path}: owner_reqs mismatch; indexed={sorted(wanted)} card={sorted(actual)}"
            )


def _check_detail_sections(
    documents: Iterable[Document], report: Report, strict: bool
) -> None:
    for document in documents:
        for pointer in document.card.get("detail_sections", []):
            if pointer.startswith("#"):
                exists = pointer[1:] in document.anchors
            else:
                exists = _normalise_heading(pointer) in document.headings
            if exists:
                continue
            message = f"{document.path}: detail_sections target not found: {pointer}"
            if strict:
                report.error(message)
            else:
                report.warn(message)


def _check_index_markdown_paths(index: Document, root: Path, report: Report) -> None:
    checked_sections = {"### AI 任务阅读包", "## 功能目录地图"}
    active_section = ""
    for number, line in index.visible_lines:
        if line.startswith("##"):
            active_section = line.strip() if line.strip() in checked_sections else ""
            continue
        if not active_section:
            continue
        for raw_path in OWNER_PATH_RE.findall(line):
            if not raw_path.startswith(("功能/", "待做工作/")):
                continue
            target = root / Path(raw_path.replace("/", "\\"))
            if not target.is_file():
                report.error(f"{index.path}:{number}: referenced Markdown does not exist: {raw_path}")


def _print_report(report: Report, docs: int, reqs: int) -> None:
    for message in report.errors:
        print(f"ERROR: {message}")
    for message in report.warnings:
        print(f"WARN: {message}")
    status = "PASS" if not report.errors else "FAIL"
    print(
        f"{status}: docs={docs} reqs={reqs} errors={len(report.errors)} "
        f"warnings={len(report.warnings)}"
    )


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--strict-details",
        action="store_true",
        help="treat unresolved detail_sections headings/anchors as errors during anchor migration",
    )
    args = parser.parse_args(argv)

    root = _project_root()
    feature_dir = root / "功能"
    index_path = feature_dir / "需求真源索引.md"
    report = Report()
    if not index_path.is_file():
        print(f"FAIL: missing requirement index: {index_path}")
        return 1

    docs: Dict[Path, Document] = {}
    for path in sorted(feature_dir.glob("*.md")):
        docs[path.resolve()] = _read_document(path, report)
    index = docs[index_path.resolve()]
    owners = _parse_owner_table(index, root, report)
    for owner in sorted(set(owners.values())):
        resolved = owner.resolve()
        if owner.is_file() and resolved not in docs:
            docs[resolved] = _read_document(owner, report)
    _check_card_ownership(docs, owners, report)
    _check_detail_sections(docs.values(), report, args.strict_details)
    _check_index_markdown_paths(index, root, report)
    _print_report(report, len(docs), len(owners))
    return 1 if report.errors else 0


if __name__ == "__main__":
    sys.exit(main())
