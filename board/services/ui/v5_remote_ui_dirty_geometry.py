from __future__ import annotations

from collections.abc import Callable

from v5_remote_ui_contract import MAX_DIRTY_RECTS_PER_FRAME


_NORMALIZED_RECTS_KEY = "_v5_normalized_dirty_rects"
_DIRTY_AREA_PIXELS_KEY = "_v5_dirty_area_pixels"


class DirtyGeometryNormalizer:
    """Validate and normalize one relay dirty batch exactly once."""

    def __init__(self, width: int, height: int, mark_metric: Callable[..., None]):
        self.width = int(width)
        self.height = int(height)
        self.mark_metric = mark_metric

    @staticmethod
    def _bounding_rect(left: dict, right: dict) -> dict:
        rects = (left, right)
        x1 = min(int(rect["x"]) for rect in rects)
        y1 = min(int(rect["y"]) for rect in rects)
        x2 = max(int(rect["x"]) + int(rect["w"]) for rect in rects)
        y2 = max(int(rect["y"]) + int(rect["h"]) for rect in rects)
        return {"x": x1, "y": y1, "w": x2 - x1, "h": y2 - y1}

    @staticmethod
    def _area(rect: dict) -> int:
        return int(rect["w"]) * int(rect["h"])

    @staticmethod
    def _overlaps(left: dict, right: dict) -> bool:
        return (
            int(left["x"]) < int(right["x"]) + int(right["w"])
            and int(right["x"]) < int(left["x"]) + int(left["w"])
            and int(left["y"]) < int(right["y"]) + int(right["h"])
            and int(right["y"]) < int(left["y"]) + int(left["h"])
        )

    @classmethod
    def _bounded_min_extra_merge(cls, rects: list[dict], limit: int) -> tuple[list[dict], int]:
        working = [dict(rect) for rect in rects]
        source_area = sum(cls._area(rect) for rect in working)
        target_limit = max(1, int(limit))
        while len(working) > target_limit:
            best: tuple[tuple[int, int, int, int], int, int, dict] | None = None
            for left_index in range(len(working) - 1):
                for right_index in range(left_index + 1, len(working)):
                    merged = cls._bounding_rect(working[left_index], working[right_index])
                    extra_pixels = (
                        cls._area(merged)
                        - cls._area(working[left_index])
                        - cls._area(working[right_index])
                    )
                    score = (extra_pixels, cls._area(merged), left_index, right_index)
                    if best is None or score < best[0]:
                        best = (score, left_index, right_index, merged)
            if best is None:
                break
            _, left_index, right_index, merged = best
            remaining = [
                rect
                for index, rect in enumerate(working)
                if index not in (left_index, right_index)
            ]
            while True:
                overlap_index = next(
                    (index for index, rect in enumerate(remaining) if cls._overlaps(merged, rect)),
                    None,
                )
                if overlap_index is None:
                    break
                merged = cls._bounding_rect(merged, remaining.pop(overlap_index))
            remaining.append(merged)
            working = remaining
        working.sort(key=lambda rect: (int(rect["y"]), int(rect["x"]), int(rect["h"]), int(rect["w"])))
        added_pixels = max(0, sum(cls._area(rect) for rect in working) - source_area)
        return working, added_pixels

    @staticmethod
    def _non_overlapping_union_rects(rects: list[dict]) -> list[dict]:
        if not rects:
            return []
        x_edges = sorted(
            {int(rect["x"]) for rect in rects}
            | {int(rect["x"]) + int(rect["w"]) for rect in rects}
        )
        strips: list[dict] = []
        previous_bands: dict[tuple[int, int], dict] = {}
        for x1, x2 in zip(x_edges, x_edges[1:]):
            intervals = sorted(
                (int(rect["y"]), int(rect["y"]) + int(rect["h"]))
                for rect in rects
                if int(rect["x"]) <= x1 and int(rect["x"]) + int(rect["w"]) >= x2
            )
            merged: list[list[int]] = []
            for y1, y2 in intervals:
                if not merged or y1 > merged[-1][1]:
                    merged.append([y1, y2])
                else:
                    merged[-1][1] = max(merged[-1][1], y2)
            current_bands: dict[tuple[int, int], dict] = {}
            for y1, y2 in merged:
                band = (y1, y2)
                previous = previous_bands.get(band)
                if previous is not None and int(previous["x"]) + int(previous["w"]) == x1:
                    previous["w"] = int(previous["w"]) + (x2 - x1)
                    current_bands[band] = previous
                    continue
                current = {"x": x1, "y": y1, "w": x2 - x1, "h": y2 - y1}
                strips.append(current)
                current_bands[band] = current
            previous_bands = current_bands
        return strips

    def source_rects(self, event: dict) -> list[dict] | None:
        source_rects = event.get("rects")
        if source_rects is None:
            source_rects = [event]
        rects: list[dict] = []
        for source in source_rects:
            x = int(source["x"])
            y = int(source["y"])
            w = int(source["w"])
            h = int(source["h"])
            if x < 0 or y < 0 or w <= 0 or h <= 0 or x + w > self.width or y + h > self.height:
                return None
            rects.append({"x": x, "y": y, "w": w, "h": h})
        return rects

    def prepare(self, event: dict) -> tuple[list[dict], int] | None:
        cached_rects = event.get(_NORMALIZED_RECTS_KEY)
        cached_pixels = event.get(_DIRTY_AREA_PIXELS_KEY)
        if isinstance(cached_rects, list) and isinstance(cached_pixels, int):
            return cached_rects, cached_pixels

        rects = self.source_rects(event)
        if not rects:
            return None
        source_rect_count = len(rects)
        rects = self._non_overlapping_union_rects(rects)
        if len(rects) > MAX_DIRTY_RECTS_PER_FRAME:
            rects, added_pixels = self._bounded_min_extra_merge(rects, MAX_DIRTY_RECTS_PER_FRAME)
            self.mark_metric("dirty_payload_bounded_merge_frames")
            self.mark_metric("dirty_payload_bounded_merge_source_rects", source_rect_count)
            self.mark_metric("dirty_payload_bounded_merge_output_rects", len(rects))
            self.mark_metric("dirty_payload_bounded_merge_added_pixels", added_pixels)
        elif len(rects) < source_rect_count:
            self.mark_metric("dirty_payload_union_frames")
            self.mark_metric("dirty_payload_union_source_rects", source_rect_count)
        normalized = [dict(rect, codec="raw") for rect in rects]
        pixels = sum(int(rect["w"]) * int(rect["h"]) for rect in normalized)
        event["rects"] = normalized
        event[_NORMALIZED_RECTS_KEY] = normalized
        event[_DIRTY_AREA_PIXELS_KEY] = pixels
        return normalized, pixels
