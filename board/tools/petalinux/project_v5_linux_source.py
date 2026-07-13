#!/usr/bin/env python3
import argparse
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

import verify_v5_linux_source as contract


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
        hashes[owner["relative"]] = contract.verify_owner(output_root, owner, print_hashes)
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
    if output_root.exists():
        shutil.rmtree(output_root)
    output_root.mkdir(parents=True)

    index_file = tempfile.NamedTemporaryFile(
        prefix="v5-linux-index-", dir=build_root, delete=False
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
        listing = git_command(
            project_root,
            index_path,
            ["ls-files", "-z", "--", "linux/kernel", "linux/realtime"],
        ).stdout
        if not listing:
            fail("Git index contains no canonical Linux owner files")
        prefix = output_root.as_posix().rstrip("/") + "/"
        git_command(
            project_root,
            index_path,
            ["checkout-index", "--stdin", "-z", "--force", "--prefix=" + prefix],
            input_data=listing,
        )
        for owner in contract.OWNERS:
            owner_overrides(project_root, output_root, owner)
        hashes = verify_projection(output_root, print_hashes=print_hashes)
        if not clean_after:
            initialize_kernel_git(output_root)
    except Exception:
        shutil.rmtree(output_root, ignore_errors=True)
        raise
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
