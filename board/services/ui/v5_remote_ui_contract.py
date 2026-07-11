from __future__ import annotations

from pathlib import Path
from typing import Union


PROTOCOL_VERSION = "8ax-remote-ui/1"
PIXEL_FORMAT = "bgra32"
RUN_DIR = Path("/run/8ax_v5_product_ui")
FRAMEBUFFER_NAME = "remote_framebuffer.bgra"
DIRTY_FIFO_NAME = "remote_dirty"
INPUT_FIFO_NAME = "remote_input"
WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


class PayloadViews:
    def __init__(self, parts: list[memoryview], total_length: int):
        self.parts = parts
        self.total_length = total_length

    def __len__(self) -> int:
        return self.total_length

    def __bool__(self) -> bool:
        return self.total_length > 0


FramePayload = Union[bytes, bytearray, memoryview, PayloadViews]
STREAM_TARGET_FPS = 30
STREAM_COALESCE_SECONDS = 1.0 / STREAM_TARGET_FPS
DIRTY_EVENT_HISTORY_LIMIT = 512
MAX_DIRTY_RECTS_PER_FRAME = 64
LARGE_DIRTY_PIXEL_RATIO = 0.75
LARGE_DIRTY_MIN_INTERVAL_SECONDS = 0.10
STREAM_IDLE_PING_SECONDS = 15.0
DIRTY_FIFO_EMPTY_SLEEP_SECONDS = 0.02
