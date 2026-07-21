from __future__ import annotations

import ctypes
import os
from pathlib import Path
from typing import Optional

from v5_remote_ui_contract import MAX_DIRTY_RECTS_PER_FRAME


PACKER_ABI_VERSION = 1
DEFAULT_LIBRARY_PATH = Path("/usr/libexec/8ax/v5_remote_ui_packer.so")
_PY_BYTES_FROM_STRING_AND_SIZE = ctypes.pythonapi.PyBytes_FromStringAndSize
_PY_BYTES_FROM_STRING_AND_SIZE.argtypes = (ctypes.c_void_p, ctypes.c_ssize_t)
_PY_BYTES_FROM_STRING_AND_SIZE.restype = ctypes.py_object
_PY_BYTES_AS_STRING = ctypes.pythonapi.PyBytes_AsString
_PY_BYTES_AS_STRING.argtypes = (ctypes.py_object,)
_PY_BYTES_AS_STRING.restype = ctypes.POINTER(ctypes.c_uint8)


class NativeDirtyPackerError(RuntimeError):
    pass


class _NativeRect(ctypes.Structure):
    _fields_ = (
        ("x", ctypes.c_uint32),
        ("y", ctypes.c_uint32),
        ("w", ctypes.c_uint32),
        ("h", ctypes.c_uint32),
    )


class NativeDirtyPacker:
    def __init__(self, library_path: Optional[Path] = None):
        configured = os.environ.get("V5_REMOTE_UI_PACKER_LIBRARY", "").strip()
        path = Path(configured) if configured else (library_path or DEFAULT_LIBRARY_PATH)
        try:
            library = ctypes.CDLL(str(path), use_errno=True)
        except OSError as exc:
            raise NativeDirtyPackerError(f"native dirty packer unavailable: {path}: {exc}") from exc
        library.v5_remote_ui_packer_abi_version.argtypes = ()
        library.v5_remote_ui_packer_abi_version.restype = ctypes.c_uint32
        library.v5_remote_ui_packer_open.argtypes = (ctypes.c_int, ctypes.c_size_t)
        library.v5_remote_ui_packer_open.restype = ctypes.c_void_p
        library.v5_remote_ui_packer_close.argtypes = (ctypes.c_void_p,)
        library.v5_remote_ui_packer_close.restype = None
        library.v5_remote_ui_packer_pack.argtypes = (
            ctypes.c_void_p,
            ctypes.c_uint32,
            ctypes.c_uint32,
            ctypes.c_size_t,
            ctypes.POINTER(_NativeRect),
            ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t),
        )
        library.v5_remote_ui_packer_pack.restype = ctypes.c_int
        abi_version = int(library.v5_remote_ui_packer_abi_version())
        if abi_version != PACKER_ABI_VERSION:
            raise NativeDirtyPackerError(
                f"native dirty packer ABI mismatch: expected={PACKER_ABI_VERSION} actual={abi_version}")
        self._library = library
        self._context = None
        self._frame_key = None

    def close(self) -> None:
        if self._context is not None:
            self._library.v5_remote_ui_packer_close(self._context)
        self._context = None
        self._frame_key = None

    def _attach(self, framebuffer_fd: int, frame_size: int) -> None:
        status = os.fstat(framebuffer_fd)
        frame_key = (int(status.st_dev), int(status.st_ino), int(status.st_size), int(frame_size))
        if self._context is not None and self._frame_key == frame_key:
            return
        self.close()
        ctypes.set_errno(0)
        context = self._library.v5_remote_ui_packer_open(framebuffer_fd, frame_size)
        if not context:
            error_number = ctypes.get_errno()
            raise NativeDirtyPackerError(
                f"native dirty packer mmap failed: errno={error_number} frame_size={frame_size}")
        self._context = context
        self._frame_key = frame_key

    def pack(self, framebuffer_fd: int, frame_size: int, width: int, height: int,
             stride: int, rects: list[dict]) -> bytes:
        if not rects or len(rects) > MAX_DIRTY_RECTS_PER_FRAME:
            raise NativeDirtyPackerError(f"invalid native dirty rect count: {len(rects)}")
        native_rects = (_NativeRect * len(rects))(*(
            _NativeRect(int(rect["x"]), int(rect["y"]), int(rect["w"]), int(rect["h"]))
            for rect in rects
        ))
        total_bytes = sum(int(rect["w"]) * int(rect["h"]) * 4 for rect in rects)
        if total_bytes <= 0:
            raise NativeDirtyPackerError("native dirty payload is empty")
        output = _PY_BYTES_FROM_STRING_AND_SIZE(None, total_bytes)
        output_pointer = _PY_BYTES_AS_STRING(output)
        output_size = ctypes.c_size_t(0)
        self._attach(framebuffer_fd, frame_size)
        ctypes.set_errno(0)
        result = self._library.v5_remote_ui_packer_pack(
            self._context,
            width,
            height,
            stride,
            native_rects,
            len(rects),
            output_pointer,
            total_bytes,
            ctypes.byref(output_size),
        )
        if result != 0 or int(output_size.value) != total_bytes:
            error_number = ctypes.get_errno()
            raise NativeDirtyPackerError(
                f"native dirty pack failed: result={result} errno={error_number} "
                f"expected={total_bytes} actual={int(output_size.value)}")
        return output

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass
