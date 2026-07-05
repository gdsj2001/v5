#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
from datetime import UTC
from datetime import datetime
from pathlib import Path

API_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(API_DIR))

import vps_ota_admin_api as api  # noqa: E402


class VpsOtaAdminApiTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory(prefix="vps_ota_admin_test_")
        self.root = Path(self.tmp.name)
        self.storage = self.root / "storage"
        self.static = self.root / "static"
        self.package = self.root / "8ax-v3-1.2.3.ota"
        self.signature = self.root / "8ax-v3-1.2.3.sig"
        self.package.write_bytes(b"ota-package-bytes\n")
        self.signature.write_bytes(b"detached-signature\n")

    def tearDown(self) -> None:
        self.tmp.cleanup()

    @staticmethod
    def sha(path: Path) -> str:
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def fields(self, scope: str = "public") -> dict[str, str]:
        return {
            "scope": scope,
            "privateId": "359764" if scope == "private" else "",
            "privateHash": "535e661e9ea313143fed0d86e9d982368ca9a70c7062823e25560f34ceef7f9d" if scope == "private" else "",
            "product": "8ax-v3",
            "channel": "stable",
            "version": "1.2.3",
            "packageSha256": self.sha(self.package),
            "signatureSha256": self.sha(self.signature),
            "packageSizeBytes": str(self.package.stat().st_size),
            "signatureSizeBytes": str(self.signature.stat().st_size),
            "signatureAlg": "ed25519",
            "keyId": "factory-key-001",
            "minCompatibleVersion": "1.0.0",
            "antiRollbackMinVersion": "1.2.0",
            "productProfile": "bus5",
            "hardwareProfile": "z20",
            "reason": "unit test release",
            "scopePolicy": api.SCOPE_POLICY,
        }

    def publish(self, scope: str = "public") -> dict:
        return api.publish_ota_package(
            self.fields(scope),
            self.package,
            self.package.name,
            self.signature,
            self.signature.name,
            storage_root=self.storage,
            static_root=self.static,
            private_root=self.root / "private",
            validate_private_binding=False,
            now=datetime(2026, 7, 3, 1, 2, 3, tzinfo=UTC),
        )

    def test_public_publish_writes_manifest_sidecars_and_static_mirror(self) -> None:
        response = self.publish("public")

        self.assertTrue(response["success"])
        self.assertEqual(response["sourceScope"], "public")
        self.assertEqual(response["targetRel"], "public/8ax-v3/stable")
        self.assertEqual(response["vpsDistributionId"], "")
        self.assertEqual(response["dnaBinding"], "")
        self.assertEqual(response["privateFolder"], "")
        manifest_path = self.storage / "public" / "8ax-v3" / "stable" / "manifest.json"
        static_manifest_path = self.static / "public" / "8ax-v3" / "stable" / "manifest.json"
        self.assertTrue(manifest_path.exists())
        self.assertEqual(manifest_path.read_bytes(), static_manifest_path.read_bytes())
        self.assertEqual(response["manifestSha256"], manifest_path.with_name("manifest.json.sha256").read_text(encoding="ascii").split()[0])
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        self.assertEqual(manifest["schema"], api.SCHEMA)
        self.assertEqual(manifest["source_scope"], "public")
        self.assertNotIn("pl_dna_hash", manifest)
        self.assertEqual(manifest["dna_binding"], "")
        self.assertEqual(manifest["package"]["sha256"], self.sha(self.package))
        self.assertEqual(manifest["signature"]["sha256"], self.sha(self.signature))
        self.assertEqual(manifest["scope_policy"], api.SCOPE_POLICY)
        package_path = self.static / "public" / "8ax-v3" / "stable" / "releases" / "1.2.3" / self.package.name
        self.assertEqual(package_path.with_name(package_path.name + ".sha256").read_text(encoding="ascii").split()[0], self.sha(self.package))

    def test_private_publish_uses_id_dna_path(self) -> None:
        response = self.publish("private")

        private_id = self.fields("private")["privateId"]
        private_hash = self.fields("private")["privateHash"]
        private_folder = f"{private_id}-{private_hash}"
        self.assertEqual(response["targetRel"], f"private/{private_folder}/ota/8ax-v3/stable")
        self.assertEqual(response["vpsDistributionId"], private_id)
        self.assertEqual(response["dnaBinding"], "server_verified")
        self.assertEqual(response["privateFolder"], private_folder)
        manifest = json.loads((self.root / "private" / private_folder / "ota" / "8ax-v3" / "stable" / "manifest.json").read_text(encoding="utf-8"))
        static_manifest = self.static / "private" / private_folder / "ota" / "8ax-v3" / "stable" / "manifest.json"
        self.assertTrue(static_manifest.exists())
        self.assertEqual(manifest["source_scope"], "private")
        self.assertEqual(manifest["vps_distribution_id"], private_id)
        self.assertNotIn("pl_dna_hash", manifest)
        self.assertEqual(manifest["dna_binding"], "server_verified")
        self.assertEqual(manifest["private_folder"], private_folder)
        self.assertEqual(manifest["selection_policy"], "private package blocks public fallback")

    def test_hash_mismatch_is_rejected_without_manifest(self) -> None:
        fields = self.fields("public")
        fields["packageSha256"] = "0" * 64

        with self.assertRaises(api.PublishError) as caught:
            api.publish_ota_package(
                fields,
                self.package,
                self.package.name,
                self.signature,
                self.signature.name,
                storage_root=self.storage,
                static_root=self.static,
                private_root=self.root / "private",
            )

        self.assertEqual(caught.exception.status, api.HTTPStatus.BAD_REQUEST)
        self.assertFalse((self.storage / "public" / "8ax-v3" / "stable" / "manifest.json").exists())

    def test_private_publish_requires_private_hash(self) -> None:
        fields = self.fields("private")
        fields["privateHash"] = ""

        with self.assertRaises(api.PublishError):
            api.publish_ota_package(
                fields,
                self.package,
                self.package.name,
                self.signature,
                self.signature.name,
                storage_root=self.storage,
                static_root=self.static,
                private_root=self.root / "private",
            )

    def test_private_publish_requires_private_id(self) -> None:
        fields = self.fields("private")
        fields["privateId"] = ""

        with self.assertRaises(api.PublishError):
            api.publish_ota_package(
                fields,
                self.package,
                self.package.name,
                self.signature,
                self.signature.name,
                storage_root=self.storage,
                static_root=self.static,
            )

    def test_drive_profile_private_publish_uses_id_dna_folder(self) -> None:
        profile = self.root / "driver_profile_map.json"
        profile.write_text(
            json.dumps(
                {
                    "schema": "v3-driver-profile-map-v1",
                    "map_scope": "private",
                    "profiles": [],
                }
            ),
            encoding="utf-8",
        )
        fields = {
            "scope": "private",
            "privateId": "359764",
            "privateHash": self.fields("private")["privateHash"],
            "profileSha256": self.sha(profile),
            "profileSizeBytes": str(profile.stat().st_size),
        }

        response = api.publish_drive_profile(
            fields,
            profile,
            profile.name,
            storage_root=self.storage / "drive-profiles",
            static_root=self.static / "drive-profiles",
            legacy_static_root=self.static / "legacy-drive-profiles",
            private_root=self.root / "private",
            validate_private_binding=False,
        )

        private_folder = f'{fields["privateId"]}-{fields["privateHash"]}'
        self.assertEqual(response["targetRel"], f"private/{private_folder}/driver_profile_map.json")
        self.assertTrue((self.root / "private" / private_folder / "driver_profile_map.json").exists())


if __name__ == "__main__":
    unittest.main()
