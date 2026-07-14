#!/usr/bin/env python3
import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import project_v5_linux_source as projection


OWNERS = (
    {"relative": "linux/kernel", "identity": "identity.json"},
    {"relative": "linux/realtime", "identity": "identity.json"},
)


class ProjectionIncrementalTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.base = Path(self.temporary.name)
        self.project = self.base / "project"
        self.build = self.base / "build"
        self.output = self.build / "current"
        self.project.mkdir()
        self.build.mkdir()
        (self.project / "linux/kernel/drivers").mkdir(parents=True)
        (self.project / "linux/realtime").mkdir(parents=True)
        self.write("linux/kernel/Makefile", "kernel\n")
        self.write("linux/kernel/drivers/keep.c", "keep\n")
        self.write("linux/kernel/drivers/change.c", "old\n")
        self.write("linux/kernel/drivers/remove.c", "remove\n")
        self.write("linux/realtime/meta.txt", "rt\n")
        self.write_identity("linux/kernel", [])
        self.write_identity("linux/realtime", [])
        self.git("init", "-q")
        self.git("add", "-A")
        self.git(
            "-c",
            "user.name=Projection Test",
            "-c",
            "user.email=projection-test@invalid",
            "commit",
            "-q",
            "-m",
            "initial",
        )
        self.patches = (
            mock.patch.object(projection.contract, "OWNERS", OWNERS),
            mock.patch.object(projection.contract, "load_identity", side_effect=self.load_identity),
            mock.patch.object(projection.contract, "verify_owner", return_value="test-hash"),
            mock.patch.object(projection.contract, "validate_rt_contract", return_value=None),
        )
        for patcher in self.patches:
            patcher.start()

    def tearDown(self):
        for patcher in reversed(self.patches):
            patcher.stop()
        self.temporary.cleanup()

    def write(self, relative, content):
        path = self.project / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")

    def write_identity(self, owner, overrides):
        self.write(
            owner + "/identity.json",
            json.dumps({"working_tree_overrides": overrides}, sort_keys=True) + "\n",
        )

    def load_identity(self, source_root, owner):
        return json.loads((source_root / owner["identity"]).read_text(encoding="utf-8"))

    def git(self, *arguments):
        subprocess.run(["git", "-C", str(self.project)] + list(arguments), check=True)

    def project_now(self):
        result = projection.project_and_verify(self.project, self.build, self.output)
        self.assertFalse((self.output / "linux/kernel/.git").exists())
        return result

    def test_git_metadata_exists_only_in_generated_build_copy(self):
        self.project_now()
        work_projection = self.build / "petalinux/output/tmp/work/v5-owner-projection"
        shutil.copytree(self.output, work_projection, symlinks=True)

        projection.initialize_kernel_build_git(work_projection, self.build, self.output)

        self.assertFalse((self.output / "linux/kernel/.git").exists())
        build_git = work_projection / "linux/kernel/.git"
        self.assertTrue(build_git.is_dir())
        subprocess.run(
            ["git", "-C", str(work_projection / "linux/kernel"), "rev-parse", "--verify", "HEAD"],
            check=True,
            stdout=subprocess.PIPE,
        )

    def test_changed_file_does_not_rewrite_unaffected_files(self):
        self.project_now()
        projected_keep = self.output / "linux/kernel/drivers/keep.c"
        projected_change = self.output / "linux/kernel/drivers/change.c"
        keep_mtime = projected_keep.stat().st_mtime_ns

        source_change = self.project / "linux/kernel/drivers/change.c"
        self.write("linux/kernel/drivers/change.c", "new\n")
        future = source_change.stat().st_mtime_ns + 2_000_000_000
        os.utime(str(source_change), ns=(future, future))
        self.write_identity("linux/kernel", ["drivers/change.c"])

        self.project_now()
        self.assertEqual("new\n", projected_change.read_text(encoding="utf-8"))
        self.assertEqual(keep_mtime, projected_keep.stat().st_mtime_ns)
        changed_mtime = projected_change.stat().st_mtime_ns

        self.project_now()
        self.assertEqual(keep_mtime, projected_keep.stat().st_mtime_ns)
        self.assertEqual(changed_mtime, projected_change.stat().st_mtime_ns)

    def test_index_add_remove_only_updates_the_delta(self):
        self.project_now()
        projected_keep = self.output / "linux/kernel/drivers/keep.c"
        keep_mtime = projected_keep.stat().st_mtime_ns
        (self.project / "linux/kernel/drivers/remove.c").unlink()
        self.write("linux/kernel/drivers/add.c", "add\n")
        self.git("add", "-A")
        self.git(
            "-c",
            "user.name=Projection Test",
            "-c",
            "user.email=projection-test@invalid",
            "commit",
            "-q",
            "-m",
            "delta",
        )

        self.project_now()
        self.assertFalse((self.output / "linux/kernel/drivers/remove.c").exists())
        self.assertEqual("add\n", (self.output / "linux/kernel/drivers/add.c").read_text(encoding="utf-8"))
        self.assertEqual(keep_mtime, projected_keep.stat().st_mtime_ns)


if __name__ == "__main__":
    unittest.main()
