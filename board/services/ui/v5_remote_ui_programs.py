#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import os
import stat
import tempfile
import threading
import time
from pathlib import Path


PROGRAM_SCHEMA_PREFIX = "v5.remote_program"
DEFAULT_PROGRAM_ROOT = Path("/opt/8ax/v5/gcode/golden")
MAX_GCODE_BYTES = 2 * 1024 * 1024
MAX_EDIT_BYTES = 1024 * 1024
SUPPORTED_EXTENSIONS = frozenset({".ngc", ".nc", ".tap", ".gcode"})


class ProgramApiError(Exception):
    def __init__(self, status: int, code: str, message: str) -> None:
        super().__init__(message)
        self.status = status
        self.code = code
        self.message = message


class ProgramFileService:
    def __init__(self, root: Path | str = DEFAULT_PROGRAM_ROOT) -> None:
        self.root = Path(root)
        self._write_lock = threading.Lock()

    def list_files(self) -> dict:
        self._ensure_root()
        files = []
        for path in sorted(self.root.iterdir(), key=lambda item: item.name.casefold()):
            if not self._is_managed_regular_file(path):
                continue
            files.append(self._file_info(path))
        return {
            "schema": f"{PROGRAM_SCHEMA_PREFIX}_list.v1",
            "program_dir": str(self.root),
            "count": len(files),
            "files": files,
        }

    def stat_file(self, file_name: str) -> dict:
        path = self._safe_path(file_name)
        if not path.exists():
            return self._missing_info(file_name, path)
        self._require_regular_file(path)
        return self._file_info(path)

    def read_file(self, file_name: str) -> dict:
        path = self._safe_path(file_name)
        if not path.exists():
            raise ProgramApiError(404, "program_file_not_found", "板端不存在这个 G-code 文件。")
        self._require_regular_file(path)
        info = self._file_info(path)
        size = int(info["size_bytes"])
        if size > MAX_EDIT_BYTES:
            raise ProgramApiError(413, "program_file_edit_limit_exceeded", "文件超过 1 MiB，不能在远程文本窗口中打开修改。")
        payload = path.read_bytes()
        try:
            text = payload.decode("utf-8")
        except UnicodeDecodeError as exc:
            raise ProgramApiError(422, "program_file_not_utf8", "文件不是 UTF-8 文本，不能在远程文本窗口中打开修改。") from exc
        return {
            "schema": f"{PROGRAM_SCHEMA_PREFIX}_file.v1",
            "file_name": file_name,
            "destination_path": str(path),
            "size_bytes": size,
            "sha256": str(info["sha256"]),
            "modified_at_unix_ms": int(info["modified_at_unix_ms"]),
            "text": text,
        }

    def upload(self, file_name: str, payload: bytes, expected_sha256: str, overwrite: bool) -> dict:
        path = self._safe_path(file_name)
        if not payload:
            raise ProgramApiError(400, "program_file_empty", "不能上传空的 G-code 文件。")
        if len(payload) > MAX_GCODE_BYTES:
            raise ProgramApiError(413, "program_file_size_limit_exceeded", "G-code 文件超过板端 2 MiB 上限。")
        expected = expected_sha256.strip().lower()
        if len(expected) != 64 or any(ch not in "0123456789abcdef" for ch in expected):
            raise ProgramApiError(400, "program_sha256_invalid", "上传请求缺少有效的 SHA256。")
        actual = hashlib.sha256(payload).hexdigest()
        if actual != expected:
            raise ProgramApiError(422, "program_sha256_mismatch", "上传内容 SHA256 与 Windows 客户端声明不一致。")

        self._ensure_root()
        with self._write_lock:
            existed = path.exists()
            if existed:
                self._require_regular_file(path)
                if not overwrite:
                    raise ProgramApiError(409, "program_file_exists", "板端已有同名文件，请确认后再覆盖。")
            temporary_path: Path | None = None
            try:
                fd, temporary_name = tempfile.mkstemp(prefix=".v5-upload-", suffix=".tmp", dir=self.root)
                temporary_path = Path(temporary_name)
                with os.fdopen(fd, "wb") as stream:
                    stream.write(payload)
                    stream.flush()
                    os.fsync(stream.fileno())
                os.chmod(temporary_path, 0o644)
                os.replace(temporary_path, path)
                temporary_path = None
                self._fsync_root()
            finally:
                if temporary_path is not None:
                    try:
                        temporary_path.unlink()
                    except FileNotFoundError:
                        pass

        info = self._file_info(path)
        if info["sha256"] != actual or info["size_bytes"] != len(payload):
            raise ProgramApiError(500, "program_upload_readback_mismatch", "板端写入后的大小或 SHA256 回读不一致。")
        return {
            "schema": f"{PROGRAM_SCHEMA_PREFIX}_upload.v1",
            "file_name": file_name,
            "destination_path": str(path),
            "size_bytes": len(payload),
            "sha256": actual,
            "overwrote": existed,
            "uploaded_at_unix_ms": int(time.time() * 1000),
        }

    def delete(self, file_name: str) -> dict:
        path = self._safe_path(file_name)
        previous = None
        deleted = False
        with self._write_lock:
            if path.exists():
                self._require_regular_file(path)
                previous = self._file_info(path)
                path.unlink()
                self._fsync_root()
                deleted = True
        return {
            "schema": f"{PROGRAM_SCHEMA_PREFIX}_delete.v1",
            "file_name": file_name,
            "destination_path": str(path),
            "deleted": deleted,
            "deleted_at_unix_ms": int(time.time() * 1000),
            "previous": previous,
        }

    def _safe_path(self, file_name: str) -> Path:
        if not isinstance(file_name, str) or not file_name or file_name != file_name.strip():
            raise ProgramApiError(400, "program_filename_invalid", "G-code 文件名不能为空或包含首尾空格。")
        if file_name in {".", ".."} or "/" in file_name or "\\" in file_name or "\x00" in file_name:
            raise ProgramApiError(400, "program_filename_invalid", "G-code 文件名必须是 basename，不能包含路径。")
        if Path(file_name).suffix.lower() not in SUPPORTED_EXTENSIONS:
            raise ProgramApiError(400, "program_extension_not_allowed", "只允许 .ngc、.nc、.tap、.gcode 文件。")
        return self.root / file_name

    def _ensure_root(self) -> None:
        try:
            self.root.mkdir(parents=True, exist_ok=True, mode=0o755)
        except OSError as exc:
            raise ProgramApiError(500, "program_directory_unavailable", "板端 G-code 程序目录不可用。") from exc
        if self.root.is_symlink() or not self.root.is_dir():
            raise ProgramApiError(500, "program_directory_invalid", "板端 G-code 程序目录不是有效目录。")

    def _is_managed_regular_file(self, path: Path) -> bool:
        if path.suffix.lower() not in SUPPORTED_EXTENSIONS:
            return False
        try:
            return stat.S_ISREG(path.lstat().st_mode)
        except OSError:
            return False

    def _require_regular_file(self, path: Path) -> None:
        try:
            mode = path.lstat().st_mode
        except OSError as exc:
            raise ProgramApiError(404, "program_file_not_found", "板端不存在这个 G-code 文件。") from exc
        if not stat.S_ISREG(mode):
            raise ProgramApiError(400, "program_file_type_invalid", "目标不是可管理的普通 G-code 文件。")

    def _file_info(self, path: Path) -> dict:
        self._require_regular_file(path)
        details = path.stat()
        size = details.st_size
        return {
            "file_name": path.name,
            "destination_path": str(path),
            "exists": True,
            "size_bytes": size,
            "modified_at_unix_ms": details.st_mtime_ns // 1_000_000,
            "editable": size <= MAX_EDIT_BYTES,
            "sha256": self._sha256(path),
        }

    @staticmethod
    def _missing_info(file_name: str, path: Path) -> dict:
        return {
            "file_name": file_name,
            "destination_path": str(path),
            "exists": False,
            "size_bytes": None,
            "modified_at_unix_ms": None,
            "editable": False,
            "sha256": None,
        }

    @staticmethod
    def _sha256(path: Path) -> str:
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(64 * 1024), b""):
                digest.update(chunk)
        return digest.hexdigest()

    def _fsync_root(self) -> None:
        try:
            fd = os.open(self.root, os.O_RDONLY)
        except OSError:
            return
        try:
            os.fsync(fd)
        finally:
            os.close(fd)
