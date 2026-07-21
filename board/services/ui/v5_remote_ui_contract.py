from __future__ import annotations

from pathlib import Path
from typing import Iterable, Union


PROTOCOL_VERSION = "8ax-remote-ui/1"
PIXEL_FORMAT = "bgra32"
RUN_DIR = Path("/run/8ax_v5_product_ui")
FRAMEBUFFER_NAME = "remote_framebuffer.bgra"
DIRTY_FIFO_NAME = "remote_dirty"
INPUT_FIFO_NAME = "remote_input"
WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


class PayloadViews:
    __slots__ = ("parts", "total_length")

    def __init__(self, parts: Iterable[memoryview], total_length: int):
        owned_parts = tuple(parts)
        if any(not part.readonly for part in owned_parts):
            raise ValueError("payload views must reference immutable owned bytes")
        if sum(len(part) for part in owned_parts) != int(total_length):
            raise ValueError("payload view length mismatch")
        object.__setattr__(self, "parts", owned_parts)
        object.__setattr__(self, "total_length", int(total_length))

    def __setattr__(self, name: str, value: object) -> None:
        raise AttributeError(f"{type(self).__name__} is immutable")

    def __len__(self) -> int:
        return self.total_length

    def __bool__(self) -> bool:
        return self.total_length > 0


FramePayload = Union[bytes, bytearray, memoryview, PayloadViews]
STREAM_TARGET_FPS = 10
STREAM_COALESCE_SECONDS = 1.0 / STREAM_TARGET_FPS
DIRTY_EVENT_HISTORY_LIMIT = 512
SHARED_DIRTY_FRAME_HISTORY_LIMIT = 8
SHARED_DIRTY_PAYLOAD_HISTORY_BYTES = 8 * 1024 * 1024
MAX_DIRTY_RECTS_PER_FRAME = 64
LARGE_DIRTY_PIXEL_RATIO = 0.75
LARGE_DIRTY_MIN_INTERVAL_SECONDS = 0.10
STREAM_IDLE_PING_SECONDS = 15.0
DIRTY_FIFO_EMPTY_SLEEP_SECONDS = 0.02
