#!/usr/bin/env python3
"""One-way incremental sync from Windows source truth to the VM v5 copy.

The Windows project tree is the source truth. The VM destination is generated
staging for build/deploy and must not be edited directly.
"""

from __future__ import annotations

import argparse
import fnmatch
import hashlib
import json
import os
import posixpath
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
import uuid
from pathlib import Path


DEFAULT_TARGET = "z20-vm"
DEFAULT_BOARD_TARGET = "z20-board"
DEFAULT_DEST = "/root/Desktop/v5"
MANIFEST_NAME = ".sync_win_source_manifest.json"

DEFAULT_EXCLUDES = (
    ".git",
    ".git/**",
    ".pytest_cache",
    ".pytest_cache/**",
    ".codex",
    ".codex/**",
    ".agents",
    ".agents/**",
    "repo_ignored",
    "repo_ignored/**",
    "**/repo_ignored",
    "**/repo_ignored/**",
    "截图",
    "截图/**",
    "vm-bak",
    "vm-bak/**",
    "**/.Xil",
    "**/.Xil/**",
    "**/*.cache",
    "**/*.cache/**",
    "**/*.gen",
    "**/*.gen/**",
    "**/*.hw",
    "**/*.hw/**",
    "**/*.ip_user_files",
    "**/*.ip_user_files/**",
    "**/*.runs",
    "**/*.runs/**",
    "**/*.sim",
    "**/*.sim/**",
    "publish",
    "publish/**",
    "**/publish",
    "**/publish/**",
    "bin",
    "bin/**",
    "**/bin",
    "**/bin/**",
    "obj",
    "obj/**",
    "**/obj",
    "**/obj/**",
    ".vs",
    ".vs/**",
    "**/.vs",
    "**/.vs/**",
    "node_modules",
    "node_modules/**",
    "**/node_modules",
    "**/node_modules/**",
    "__pycache__",
    "__pycache__/**",
    "**/__pycache__",
    "**/__pycache__/**",
    "*.pyc",
    "**/*.pyc",
    "*.pyo",
    "**/*.pyo",
)


def project_root_from_script() -> Path:
    return Path(__file__).resolve().parents[2]


def rel_posix(path: Path, root: Path) -> str:
    return path.relative_to(root).as_posix()


def excluded(rel: str, patterns: tuple[str, ...]) -> bool:
    rel = rel.strip("/")
    for pattern in patterns:
        pattern = pattern.strip("/")
        if not pattern:
            continue
        if fnmatch.fnmatch(rel, pattern):
            return True
        if pattern.endswith("/**"):
            prefix = pattern[:-3].rstrip("/")
            if rel == prefix or rel.startswith(prefix + "/"):
                return True
    return False


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as fp:
        for chunk in iter(lambda: fp.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def build_manifest(root: Path, patterns: tuple[str, ...]) -> dict[str, dict[str, object]]:
    manifest: dict[str, dict[str, object]] = {}
    for dirpath, dirnames, filenames in os.walk(root):
        current = Path(dirpath)
        kept_dirs = []
        for name in dirnames:
            child = current / name
            rel = rel_posix(child, root)
            if not excluded(rel, patterns):
                kept_dirs.append(name)
        dirnames[:] = kept_dirs
        for name in filenames:
            path = current / name
            rel = rel_posix(path, root)
            if excluded(rel, patterns) or rel == MANIFEST_NAME:
                continue
            stat = path.stat()
            manifest[rel] = {
                "size": stat.st_size,
                "mtime_ns": stat.st_mtime_ns,
                "sha256": sha256_file(path),
            }
    return dict(sorted(manifest.items()))


def run(args: list[str], *, input_text: str | None = None, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        args,
        input=input_text,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=check,
    )


def bash_posix_path(bash: str, path: Path) -> str:
    result = run([bash, "-lc", "cygpath -u -- " + shlex.quote(str(path))])
    value = result.stdout.strip()
    if not value:
        raise RuntimeError(f"cygpath returned empty path for {path}")
    return value


def refresh_board_owner_files_before_sync(
    root: Path,
    *,
    bash: str,
    board_target: str,
    board_port: str,
) -> int:
    board_root = root / "board"
    script = board_root / "tools" / "deploy" / "push_v5_runtime_to_board.sh"
    if not script.is_file():
        print(f"error: missing board owner refresh script: {script}", file=sys.stderr)
        return 2
    env = os.environ.copy()
    env["V5_REPO_ROOT"] = bash_posix_path(bash, board_root)
    env["V5_LOCAL_OWNER_BACKUP_DIR"] = bash_posix_path(bash, root / "bak")
    env["V5_BOARD_SSH"] = board_target
    env["V5_BOARD_SSH_PORT"] = board_port
    bash_script = bash_posix_path(bash, script)
    print(f"refresh_board_owner_files board={board_target} port={board_port}")
    result = subprocess.run(
        [bash, bash_script, "--refresh-board-owner-files"],
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
        check=False,
    )
    if result.stdout.strip():
        print(result.stdout.rstrip())
    if result.stderr.strip():
        print(result.stderr.rstrip(), file=sys.stderr)
    return result.returncode


def remote_manifest(ssh: str, target: str, dest: str) -> dict[str, dict[str, object]]:
    path = posixpath.join(dest, MANIFEST_NAME)
    cmd = f"test -f {shlex.quote(path)} && cat {shlex.quote(path)} || true"
    result = run([ssh, target, cmd], check=False)
    if result.returncode != 0 or not result.stdout.strip():
        return {}
    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError:
        return {}
    files = payload.get("files")
    return files if isinstance(files, dict) else {}


def remote_drift_probe_script() -> str:
    return r"""
import hashlib
import json
import os
import sys

dest = os.path.abspath(sys.argv[1])
manifest_path = sys.argv[2]
with open(manifest_path, "r", encoding="utf-8") as fp:
    payload = json.load(fp)
files = payload.get("files") or {}
drift = []

def file_sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as fp:
        for chunk in iter(lambda: fp.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()

for rel, record in sorted(files.items()):
    if not rel or rel.startswith("/") or ".." in rel.split("/"):
        drift.append(rel)
        continue
    path = os.path.abspath(os.path.join(dest, *rel.split("/")))
    if path != dest and not path.startswith(dest + os.sep):
        drift.append(rel)
        continue
    try:
        stat = os.stat(path)
    except OSError:
        drift.append(rel)
        continue
    if not os.path.isfile(path):
        drift.append(rel)
        continue
    try:
        expected_size = int(record.get("size", -1))
    except (TypeError, ValueError):
        expected_size = -1
    if expected_size != stat.st_size:
        drift.append(rel)
        continue
    if str(record.get("sha256") or "") != file_sha256(path):
        drift.append(rel)

print(json.dumps(drift, ensure_ascii=False, sort_keys=True))
"""


def remote_drift_files(
    ssh: str,
    scp: str,
    target: str,
    dest: str,
    manifest: dict[str, dict[str, object]],
    archive_dir: Path,
) -> list[str]:
    archive_dir.mkdir(parents=True, exist_ok=True)
    remote_tmp = f"/tmp/v5_win_source_manifest_{uuid.uuid4().hex}.json"
    with tempfile.NamedTemporaryFile(
        "w",
        encoding="utf-8",
        dir=archive_dir,
        prefix="v5_sync_manifest_",
        suffix=".json",
        delete=False,
    ) as fp:
        local_manifest = Path(fp.name)
        json.dump({"files": manifest}, fp, ensure_ascii=False, sort_keys=True)
    try:
        run([scp, str(local_manifest), f"{target}:{remote_tmp}"])
        result = run(
            [ssh, target, "python3", "-", dest, remote_tmp],
            input_text=remote_drift_probe_script(),
        )
    finally:
        run([ssh, target, f"rm -f {shlex.quote(remote_tmp)}"], check=False)
        try:
            local_manifest.unlink()
        except OSError:
            pass
    try:
        payload = json.loads(result.stdout or "[]")
    except json.JSONDecodeError:
        return []
    if not isinstance(payload, list):
        return []
    return sorted(rel for rel in payload if isinstance(rel, str) and rel in manifest)


def make_archive(root: Path, changed: list[str], manifest: dict[str, dict[str, object]], archive_dir: Path) -> Path:
    archive_dir.mkdir(parents=True, exist_ok=True)
    archive = archive_dir / f"v5_source_delta_{uuid.uuid4().hex}.tar.gz"
    manifest_payload = {
        "schema": "v5.win_source_sync_manifest.v1",
        "source": "windows_project_root",
        "files": manifest,
    }
    with tempfile.TemporaryDirectory(prefix="v5_sync_manifest_") as tmp:
        manifest_path = Path(tmp) / MANIFEST_NAME
        manifest_path.write_text(json.dumps(manifest_payload, ensure_ascii=False, sort_keys=True, indent=2), encoding="utf-8")
        with tarfile.open(archive, "w:gz") as tar:
            for rel in changed:
                tar.add(root / Path(rel), arcname=rel, recursive=False)
            tar.add(manifest_path, arcname=MANIFEST_NAME, recursive=False)
    return archive


def changed_files(local: dict[str, dict[str, object]], remote: dict[str, dict[str, object]]) -> list[str]:
    changed = []
    for rel, record in local.items():
        old = remote.get(rel)
        if not old or old.get("sha256") != record.get("sha256") or old.get("size") != record.get("size"):
            changed.append(rel)
    return changed


def deleted_files(local: dict[str, dict[str, object]], remote: dict[str, dict[str, object]]) -> list[str]:
    return sorted(rel for rel in remote if rel not in local and rel != MANIFEST_NAME)


def remote_safe_delete_script(deletes: list[str]) -> str:
    return f"""
import json
import os
import shutil
import sys

dest = os.path.abspath(sys.argv[1])
items = json.loads({json.dumps(json.dumps(deletes, ensure_ascii=False))})
if dest in ('', '/', '/root', '/home'):
    raise SystemExit('unsafe destination: ' + dest)

removed = 0
parents = []
for rel in items:
    if not rel or rel.startswith('/') or '..' in rel.split('/'):
        continue
    path = os.path.abspath(os.path.join(dest, rel))
    if path != dest and not path.startswith(dest + os.sep):
        continue
    if os.path.isdir(path) and not os.path.islink(path):
        shutil.rmtree(path)
        removed += 1
    elif os.path.lexists(path):
        os.unlink(path)
        removed += 1
    parents.append(os.path.dirname(path))

for path in sorted(set(parents), key=len, reverse=True):
    while path.startswith(dest + os.sep):
        try:
            os.rmdir(path)
        except OSError:
            break
        path = os.path.dirname(path)
print('deleted=' + str(removed))
"""


def remote_fix_modes(ssh: str, target: str, dest: str) -> None:
    qdest = shlex.quote(dest)
    cmd = (
        f"find {qdest} -type f "
        "\\( -name '*.sh' -o -name '*.py' -o -path '*/init.d/*' \\) "
        "-exec chmod u+x {} +"
    )
    run([ssh, target, cmd])


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Incrementally sync Windows v5 source truth to the VM copy.")
    parser.add_argument("--project-root", type=Path, default=project_root_from_script(), help="Windows source truth root, default: D:/v5")
    parser.add_argument("--target", default=os.environ.get("V5_VM_SSH_TARGET", DEFAULT_TARGET), help="SSH target alias")
    parser.add_argument("--board-target", default=os.environ.get("V5_BOARD_SSH", DEFAULT_BOARD_TARGET), help="Board SSH target alias")
    parser.add_argument("--board-port", default=os.environ.get("V5_BOARD_SSH_PORT", "22"), help="Board SSH port")
    parser.add_argument("--dest", default=os.environ.get("V5_VM_SYNC_DEST", DEFAULT_DEST), help="VM destination directory")
    parser.add_argument("--ssh", default=os.environ.get("V5_SSH", "ssh"), help="ssh executable")
    parser.add_argument("--scp", default=os.environ.get("V5_SCP", "scp"), help="scp executable")
    parser.add_argument("--bash", default=os.environ.get("V5_BASH", "bash"), help="bash executable for project shell scripts")
    parser.add_argument("--archive-dir", type=Path, default=None, help="Local temp archive directory")
    parser.add_argument("--exclude", action="append", default=[], help="Extra exclude pattern, may repeat")
    parser.add_argument("--no-delete", action="store_true", help="Do not delete files that vanished locally since the last sync")
    parser.add_argument(
        "--skip-board-owner-refresh",
        action="store_true",
        help="Skip refreshing board-owned parameter files before VM sync",
    )
    parser.add_argument(
        "--skip-remote-drift-check",
        action="store_true",
        help="Trust the VM manifest and skip hashing remote files for manual-edit drift",
    )
    parser.add_argument("--dry-run", action="store_true", help="Print delta summary without transferring")
    parser.add_argument("--keep-archive", action="store_true", help="Keep generated local delta archive")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    root = args.project_root.resolve()
    if not (root / "board" / "app").is_dir():
        print(f"error: project root does not look like v5: {root}", file=sys.stderr)
        return 2
    if args.dry_run:
        print("dry-run: skipped board owner refresh")
    elif not args.skip_board_owner_refresh:
        refresh_code = refresh_board_owner_files_before_sync(
            root,
            bash=args.bash,
            board_target=args.board_target,
            board_port=args.board_port,
        )
        if refresh_code != 0:
            print("error: board owner refresh failed; VM sync not started", file=sys.stderr)
            return refresh_code
    archive_dir = args.archive_dir or (root / "repo_ignored" / "temp" / "vm_sync")
    patterns = tuple(DEFAULT_EXCLUDES) + tuple(args.exclude)

    local = build_manifest(root, patterns)
    remote = remote_manifest(args.ssh, args.target, args.dest)
    changed = changed_files(local, remote)
    drift: list[str] = []
    if remote and not args.skip_remote_drift_check:
        drift = remote_drift_files(args.ssh, args.scp, args.target, args.dest, local, archive_dir)
        if drift:
            changed = sorted(set(changed).union(drift))
    deletes = [] if args.no_delete else deleted_files(local, remote)

    print(
        "sync_plan "
        f"root={root} target={args.target}:{args.dest} "
        f"local_files={len(local)} changed={len(changed)} drift={len(drift)} deleted={len(deletes)}"
    )
    if args.dry_run:
        for rel in changed[:40]:
            print("change " + rel)
        if len(changed) > 40:
            print(f"change ... {len(changed) - 40} more")
        for rel in deletes[:40]:
            print("delete " + rel)
        if len(deletes) > 40:
            print(f"delete ... {len(deletes) - 40} more")
        return 0

    archive = make_archive(root, changed, local, archive_dir)
    remote_tmp = f"/tmp/v5_win_source_sync_{uuid.uuid4().hex}.tar.gz"
    try:
        run([args.ssh, args.target, f"mkdir -p {shlex.quote(args.dest)}"])
        run([args.scp, str(archive), f"{args.target}:{remote_tmp}"])
        run([
            args.ssh,
            args.target,
            f"tar -xzf {shlex.quote(remote_tmp)} -C {shlex.quote(args.dest)} && rm -f {shlex.quote(remote_tmp)}",
        ])
        if deletes:
            script = remote_safe_delete_script(deletes)
            result = run([args.ssh, args.target, "python3", "-", args.dest], input_text=script)
            if result.stdout.strip():
                print(result.stdout.strip())
        remote_fix_modes(args.ssh, args.target, args.dest)
        print(f"synced changed={len(changed)} deleted={len(deletes)} manifest={args.dest}/{MANIFEST_NAME}")
    finally:
        if not args.keep_archive:
            try:
                archive.unlink()
            except OSError:
                pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
