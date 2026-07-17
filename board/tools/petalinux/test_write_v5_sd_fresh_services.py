#!/usr/bin/env python3
"""Focused fresh-rootfs startup-link contract for the Position publisher."""

from __future__ import annotations

import re
import os
import subprocess
import tempfile
from pathlib import Path


BOARD_ROOT = Path(__file__).resolve().parents[2]
WRITE_SD = BOARD_ROOT / "tools" / "petalinux" / "write_v5_sd_card.sh"
MANIFEST = BOARD_ROOT / "config" / "deploy" / "v5_runtime_deploy_manifest.tsv"
SERVICE = "v5-position-status-publisher"


def main() -> int:
    write_sd = WRITE_SD.read_text(encoding="utf-8")
    manifest_rows = [
        row.split("\t")
        for row in MANIFEST.read_text(encoding="utf-8").splitlines()
        if row and not row.startswith("#")
    ]
    service_rows = [row for row in manifest_rows if row[2] == f"/etc/init.d/{SERVICE}"]
    assert len(service_rows) == 1, "Position init must have exactly one manifest owner"

    calls = re.findall(
        rf"^enable_service\s+{re.escape(SERVICE)}\s+(\d+)\s+(\d+)\s*$",
        write_sd,
        flags=re.MULTILINE,
    )
    assert calls == [("92", "18")], "fresh-SD must enable Position exactly once as S92/K18"
    function_start = write_sd.index("enable_service() {")
    function_end = write_sd.index("\n}\n\nenable_service v5-linuxcnc-command-gate", function_start) + 3
    enable_service_function = write_sd[function_start:function_end]
    assert 'ln -s "../init.d/$name" "$dir/S${start_prio}${name}"' in enable_service_function
    assert 'ln -s "../init.d/$name" "$dir/K${stop_prio}${name}"' in enable_service_function
    position_call = f"enable_service {SERVICE} {calls[0][0]} {calls[0][1]}"

    with tempfile.TemporaryDirectory() as temporary:
        rootfs = Path(temporary)
        init_dir = rootfs / "etc" / "init.d"
        init_dir.mkdir(parents=True)
        (init_dir / SERVICE).write_text("#!/bin/sh\n", encoding="utf-8")
        for level in (0, 1, 2, 3, 4, 5, 6):
            (rootfs / "etc" / f"rc{level}.d").mkdir(parents=True)
        for level in (2, 3, 4, 5):
            (rootfs / "etc" / f"rc{level}.d" / f"S01{SERVICE}").write_text("old\n")
            (rootfs / "etc" / f"rc{level}.d" / f"S99{SERVICE}").write_text("duplicate\n")
        for level in (0, 1, 6):
            (rootfs / "etc" / f"rc{level}.d" / f"K01{SERVICE}").write_text("old\n")
            (rootfs / "etc" / f"rc{level}.d" / f"K99{SERVICE}").write_text("duplicate\n")
        harness = f"set -eu\nrootfs_stage=$1\n{enable_service_function}\n{position_call}\n"
        subprocess.run(
            ["sh", "-c", harness, "v5-fresh-service-test", rootfs.as_posix()],
            check=True,
            env=os.environ.copy(),
        )
        links = sorted(rootfs.glob(f"etc/rc*.d/*{SERVICE}"))
        assert len(links) == 7, "fresh rootfs must contain four start and three stop links"
        expected_names = {
            *(f"S92{SERVICE}" for _level in (2, 3, 4, 5)),
            *(f"K18{SERVICE}" for _level in (0, 1, 6)),
        }
        assert {link.name for link in links} == expected_names
        assert sum(link.name.startswith("S92") for link in links) == 4
        assert sum(link.name.startswith("K18") for link in links) == 3
        assert not list(rootfs.glob(f"etc/rc*.d/[SK]01{SERVICE}"))
        assert not list(rootfs.glob(f"etc/rc*.d/[SK]99{SERVICE}"))
        if os.name != "nt":
            assert all(link.is_symlink() for link in links)
            assert all(link.readlink() == Path(f"../init.d/{SERVICE}") for link in links)

    print("V5_FRESH_SD_POSITION_SERVICE_LINKS_OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
