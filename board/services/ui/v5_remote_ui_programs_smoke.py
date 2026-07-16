#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import tempfile
from pathlib import Path

from v5_remote_ui_programs import MAX_EDIT_BYTES, ProgramApiError, ProgramFileService


def expect_error(code: str, action) -> None:
    try:
        action()
    except ProgramApiError as exc:
        if exc.code != code:
            raise AssertionError(f"expected {code}, got {exc.code}") from exc
        return
    raise AssertionError(f"expected {code}")


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="v5-remote-programs-") as temporary:
        root = Path(temporary) / "gcode" / "golden"
        service = ProgramFileService(root)
        assert service.list_files()["count"] == 0

        payload = b"G0 X0\nM2\n"
        sha256 = hashlib.sha256(payload).hexdigest()
        uploaded = service.upload("remote_button_test.ngc", payload, sha256, overwrite=False)
        assert uploaded["destination_path"] == str(root / "remote_button_test.ngc")
        assert uploaded["size_bytes"] == len(payload)
        assert uploaded["sha256"] == sha256
        assert uploaded["overwrote"] is False
        assert not list(root.glob(".v5-upload-*.tmp"))

        listed = service.list_files()
        assert listed["program_dir"] == str(root)
        assert listed["count"] == 1
        assert listed["files"][0]["file_name"] == "remote_button_test.ngc"
        assert service.stat_file("remote_button_test.ngc")["exists"] is True
        assert service.read_file("remote_button_test.ngc")["text"] == payload.decode("utf-8")

        expect_error("program_file_exists", lambda: service.upload("remote_button_test.ngc", payload, sha256, overwrite=False))
        expect_error("program_filename_invalid", lambda: service.stat_file("../escape.ngc"))
        expect_error("program_extension_not_allowed", lambda: service.stat_file("notes.txt"))
        expect_error("program_file_empty", lambda: service.upload("empty.ngc", b"", hashlib.sha256(b"").hexdigest(), overwrite=False))
        expect_error("program_sha256_mismatch", lambda: service.upload("bad.ngc", payload, "0" * 64, overwrite=False))

        replacement = b"G1 X1\nM2\n"
        replacement_sha = hashlib.sha256(replacement).hexdigest()
        overwritten = service.upload("remote_button_test.ngc", replacement, replacement_sha, overwrite=True)
        assert overwritten["overwrote"] is True
        assert service.read_file("remote_button_test.ngc")["sha256"] == replacement_sha

        large_path = root / "large.ngc"
        large_path.write_bytes(b"%" * (MAX_EDIT_BYTES + 1))
        assert service.stat_file("large.ngc")["editable"] is False
        expect_error("program_file_edit_limit_exceeded", lambda: service.read_file("large.ngc"))

        binary_path = root / "binary.nc"
        binary_path.write_bytes(b"\xff\xfe")
        expect_error("program_file_not_utf8", lambda: service.read_file("binary.nc"))

        deleted = service.delete("remote_button_test.ngc")
        assert deleted["deleted"] is True
        assert deleted["previous"]["sha256"] == replacement_sha
        assert service.delete("remote_button_test.ngc")["deleted"] is False
        assert service.stat_file("remote_button_test.ngc")["exists"] is False

    print("PASS: v5 remote program file service")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
