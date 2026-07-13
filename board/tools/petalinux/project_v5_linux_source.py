#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

import verify_v5_linux_source as contract


STATE_NAME = ".v5-projection-state.json"
STATE_SCHEMA = "v5-linux-projection-state-v1"


def fail(message):
    raise ValueError(message)


def is_within(path, root):
    try:
        return os.path.commonpath((str(path), str(root))) == str(root)
    except ValueError:
        return False


def validate_paths(project_root, build_root, output_root):
    if output_root.is_symlink():
        fail("projection output must not be a symlink: %s" % output_root)
    project_root = project_root.resolve()
    build_root = build_root.resolve()
    output_root = output_root.resolve()
    if not (project_root / ".git/index").is_file():
        fail("project Git index is unavailable: %s" % project_root)
    if output_root == build_root or not is_within(output_root, build_root):
        fail("projection output must stay below the build root: %s" % output_root)
    if is_within(output_root, project_root) or is_within(project_root, output_root):
        fail("projection output overlaps the Windows source owner")
    return project_root, build_root, output_root


def git_command(project_root, index_path, arguments, input_data=None):
    environment = os.environ.copy()
    environment["GIT_INDEX_FILE"] = str(index_path)
    environment["GIT_OPTIONAL_LOCKS"] = "0"
    command = ["git", "-c", "core.fsmonitor=false", "-C", str(project_root)] + arguments
    try:
        return subprocess.run(
            command,
            input=input_data,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=environment,
            check=True,
        )
    except subprocess.CalledProcessError as exc:
        detail = exc.stderr.decode("utf-8", errors="replace").strip()
        fail("Git projection command failed: %s" % detail)


def nul_records(data):
    return [record for record in data.split(b"\0") if record]


def indexed_symlinks(project_root, index_path):
    records = git_command(
        project_root,
        index_path,
        ["ls-files", "-s", "-z", "--", "linux/kernel", "linux/realtime"],
    ).stdout
    paths = set()
    for record in nul_records(records):
        metadata, path = record.split(b"\t", 1)
        if metadata.split(b" ", 1)[0] == b"120000":
            paths.add(path)
    return paths


def vm_share_inaccessible_paths(project_root):
    paths = set()
    for owner in contract.OWNERS:
        source_root = project_root / owner["relative"]
        identity = contract.load_identity(source_root, owner)
        for relative in identity.get("vm_share_inaccessible_paths", []):
            paths.add((Path(owner["relative"]) / relative).as_posix().encode("utf-8"))
    return paths


def below_indexed_symlink(path, symlinks):
    return any(path == link or path.startswith(link + b"/") for link in symlinks)


def copy_override(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.is_symlink() or destination.exists():
        if destination.is_dir() and not destination.is_symlink():
            shutil.rmtree(destination)
        else:
            destination.unlink()
    if source.is_symlink():
        destination.symlink_to(os.readlink(source))
    elif source.is_file():
        shutil.copy2(source, destination)
    else:
        fail("registered source override is unavailable: %s" % source)


def source_signature(path):
    if path.is_symlink():
        return "symlink:%s" % os.readlink(path)
    if not path.is_file():
        fail("registered projection source is unavailable: %s" % path)
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return "file:%o:%s" % (path.stat().st_mode & 0o777, digest.hexdigest())


def registered_overrides(project_root):
    overrides = {}
    for owner in contract.OWNERS:
        source_root = project_root / owner["relative"]
        identity = contract.load_identity(source_root, owner)
        relative_paths = identity.get("working_tree_overrides")
        if not isinstance(relative_paths, list) or len(relative_paths) != len(set(relative_paths)):
            fail("invalid working_tree_overrides: %s" % owner["relative"])
        relative_paths = list(relative_paths) + [owner["identity"]]
        for relative in relative_paths:
            path = Path(relative)
            if path.is_absolute() or ".." in path.parts:
                fail("source override escaped its owner: %s" % relative)
            projected = (Path(owner["relative"]) / path).as_posix()
            source = source_root / path
            overrides[projected] = (source, "owner:%s" % source_signature(source))
    return overrides


def indexed_projection_entries(project_root, index_path):
    records = git_command(
        project_root,
        index_path,
        ["ls-files", "-s", "-z", "--", "linux/kernel", "linux/realtime"],
    ).stdout
    entries = {}
    encoded_paths = {}
    for record in nul_records(records):
        metadata, encoded_path = record.split(b"\t", 1)
        mode, object_id, stage = metadata.split(b" ", 2)
        if stage != b"0":
            fail("unmerged source index entry: %s" % encoded_path.decode("utf-8", errors="replace"))
        try:
            path = encoded_path.decode("utf-8")
        except UnicodeDecodeError:
            fail("non-UTF-8 source path is unsupported")
        entries[path] = "git:%s:%s" % (mode.decode("ascii"), object_id.decode("ascii"))
        encoded_paths[path] = encoded_path
    return entries, encoded_paths


def load_projection_state(output_root):
    state_path = output_root / STATE_NAME
    if not state_path.is_file():
        return None
    try:
        state = json.loads(state_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        fail("invalid projection state %s: %s" % (state_path, exc))
    entries = state.get("entries")
    if state.get("schema") != STATE_SCHEMA or not isinstance(entries, dict):
        fail("invalid projection state schema: %s" % state_path)
    if any(not isinstance(path, str) or not isinstance(value, str) for path, value in entries.items()):
        fail("invalid projection state entries: %s" % state_path)
    return entries


def write_projection_state(output_root, entries):
    state_path = output_root / STATE_NAME
    temporary = state_path.with_name(state_path.name + ".new")
    temporary.write_text(
        json.dumps({"schema": STATE_SCHEMA, "entries": entries}, sort_keys=True, indent=2) + "\n",
        encoding="utf-8",
    )
    os.replace(str(temporary), str(state_path))


def projected_path_exists(path):
    return path.is_symlink() or path.exists()


def remove_projected_path(path):
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.is_dir():
        shutil.rmtree(path)


def apply_projection_delta(
    project_root, index_path, output_root, desired_entries, encoded_paths, overrides, previous_entries
):
    previous_entries = previous_entries or {}
    removed = sorted(set(previous_entries) - set(desired_entries), key=lambda item: item.count("/"), reverse=True)
    changed = {
        path
        for path, signature in desired_entries.items()
        if previous_entries.get(path) != signature
        or not projected_path_exists(output_root / Path(path))
    }
    for relative in removed:
        remove_projected_path(output_root / Path(relative))
    indexed_changed = sorted(changed - set(overrides))
    for relative in indexed_changed:
        remove_projected_path(output_root / Path(relative))
    if indexed_changed:
        payload = b"\0".join(encoded_paths[path] for path in indexed_changed) + b"\0"
        prefix = output_root.as_posix().rstrip("/") + "/"
        git_command(
            project_root,
            index_path,
            ["checkout-index", "--stdin", "-z", "--force", "--prefix=" + prefix],
            input_data=payload,
        )
    for relative in sorted(changed & set(overrides)):
        source, unused_signature = overrides[relative]
        copy_override(source, output_root / Path(relative))
    print(
        "V5_LINUX_PROJECTION_DELTA changed=%d removed=%d unchanged=%d"
        % (len(changed), len(removed), len(desired_entries) - len(changed))
    )


def owner_overrides(project_root, output_root, owner):
    source_root = project_root / owner["relative"]
    identity = contract.load_identity(source_root, owner)
    overrides = identity.get("working_tree_overrides")
    if not isinstance(overrides, list) or len(overrides) != len(set(overrides)):
        fail("invalid working_tree_overrides: %s" % owner["relative"])
    for relative in overrides:
        path = Path(relative)
        if path.is_absolute() or ".." in path.parts:
            fail("source override escaped its owner: %s" % relative)
        copy_override(source_root / path, output_root / owner["relative"] / path)
    copy_override(
        source_root / owner["identity"],
        output_root / owner["relative"] / owner["identity"],
    )


def verify_projection(output_root, print_hashes=False):
    hashes = {}
    for owner in contract.OWNERS:
        hashes[owner["relative"]] = contract.verify_owner(
            output_root, owner, print_hashes, allow_build_git=True
        )
    contract.validate_rt_contract(output_root / "linux/realtime")
    print(
        "V5_LINUX_PROJECTION_OK kernel=%s realtime=%s"
        % (hashes["linux/kernel"], hashes["linux/realtime"])
    )
    return hashes


def initialize_kernel_git(output_root):
    kernel_root = output_root / "linux/kernel"
    environment = os.environ.copy()
    for name in ("GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE"):
        environment.pop(name, None)
    environment["GIT_OPTIONAL_LOCKS"] = "0"
    commands = (
        ["git", "init", "-q"],
        ["git", "symbolic-ref", "HEAD", "refs/heads/master"],
        ["git", "add", "-A"],
    )
    for command in commands:
        try:
            subprocess.run(
                command,
                cwd=str(kernel_root),
                env=environment,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=True,
            )
        except subprocess.CalledProcessError as exc:
            detail = exc.stderr.decode("utf-8", errors="replace").strip()
            fail("failed to initialize projected kernel Git metadata: %s" % detail)
    head_exists = subprocess.run(
        ["git", "rev-parse", "--verify", "HEAD"],
        cwd=str(kernel_root),
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    ).returncode == 0
    changed = subprocess.run(
        ["git", "diff", "--cached", "--quiet"],
        cwd=str(kernel_root),
        env=environment,
    ).returncode != 0
    if not head_exists or changed:
        try:
            subprocess.run(
                [
                    "git",
                    "-c",
                    "user.name=V5 Build Projection",
                    "-c",
                    "user.email=v5-build-projection@invalid",
                    "commit",
                    "-q",
                    "-m",
                    "V5 canonical kernel projection",
                ],
                cwd=str(kernel_root),
                env=environment,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=True,
            )
        except subprocess.CalledProcessError as exc:
            detail = exc.stderr.decode("utf-8", errors="replace").strip()
            fail("failed to commit projected kernel Git metadata: %s" % detail)
    head = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=str(kernel_root),
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    ).stdout.decode("ascii").strip()
    print("V5_LINUX_KERNEL_BUILD_GIT_OK head=%s" % head)


def project_and_verify(
    project_root, build_root, output_root, clean_after=False, print_hashes=False
):
    project_root, build_root, output_root = validate_paths(project_root, build_root, output_root)
    output_root.parent.mkdir(parents=True, exist_ok=True)

    index_file = tempfile.NamedTemporaryFile(
        prefix="v5-linux-index-", dir=output_root.parent, delete=False
    )
    index_path = Path(index_file.name)
    index_file.close()
    try:
        shutil.copyfile(project_root / ".git/index", index_path)
        git_command(project_root, index_path, ["update-index", "--no-fsmonitor"])
        symlinks = indexed_symlinks(project_root, index_path)
        inaccessible = vm_share_inaccessible_paths(project_root)
        deleted_raw = git_command(
            project_root,
            index_path,
            ["ls-files", "--deleted", "-z", "--", "linux/kernel", "linux/realtime"],
        ).stdout
        deleted_paths = [
            path
            for path in nul_records(deleted_raw)
            if path not in symlinks and path not in inaccessible
        ]
        deleted = b"\0".join(deleted_paths) + (b"\0" if deleted_paths else b"")
        if deleted:
            git_command(
                project_root,
                index_path,
                ["update-index", "--force-remove", "-z", "--stdin"],
                input_data=deleted,
            )
        untracked_raw = git_command(
            project_root,
            index_path,
            [
                "ls-files",
                "--others",
                "--exclude-standard",
                "-z",
                "--",
                "linux/kernel",
                "linux/realtime",
            ],
        ).stdout
        untracked = [
            path
            for path in nul_records(untracked_raw)
            if path not in inaccessible and not below_indexed_symlink(path, symlinks)
        ]
        if untracked:
            fail(
                "untracked Linux source is outside the canonical Git snapshot: %s"
                % untracked[0].decode("utf-8", errors="replace")
            )
        indexed_entries, encoded_paths = indexed_projection_entries(project_root, index_path)
        if not indexed_entries:
            fail("Git index contains no canonical Linux owner files")
        overrides = registered_overrides(project_root)
        desired_entries = dict(indexed_entries)
        desired_entries.update({path: value[1] for path, value in overrides.items()})
        previous_entries = None
        if output_root.exists():
            previous_entries = load_projection_state(output_root)
            if previous_entries is None:
                try:
                    verify_projection(output_root)
                    previous_entries = dict(desired_entries)
                    print("V5_LINUX_PROJECTION_STATE_MIGRATED output=%s" % output_root)
                except (OSError, ValueError):
                    shutil.rmtree(output_root)
                    print("V5_LINUX_PROJECTION_REPAIRED reason=untrusted-state")
        output_root.mkdir(parents=True, exist_ok=True)
        apply_projection_delta(
            project_root,
            index_path,
            output_root,
            desired_entries,
            encoded_paths,
            overrides,
            previous_entries,
        )
        hashes = verify_projection(output_root, print_hashes=print_hashes)
        if not clean_after:
            initialize_kernel_git(output_root)
        write_projection_state(output_root, desired_entries)
    finally:
        try:
            index_path.unlink()
        except FileNotFoundError:
            pass
    if clean_after:
        shutil.rmtree(output_root)
        print("V5_LINUX_PROJECTION_CLEAN output=%s" % output_root)
    return hashes


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-root", type=Path, required=True)
    parser.add_argument("--build-root", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--clean-after", action="store_true")
    parser.add_argument("--print-source-hashes", action="store_true")
    args = parser.parse_args()
    try:
        project_and_verify(
            args.project_root,
            args.build_root,
            args.output_root,
            clean_after=args.clean_after,
            print_hashes=args.print_source_hashes,
        )
    except (OSError, ValueError) as exc:
        raise SystemExit("V5 Linux source projection failed: %s" % exc)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
