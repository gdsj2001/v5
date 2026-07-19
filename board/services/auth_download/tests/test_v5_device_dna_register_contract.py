from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
from types import SimpleNamespace


AUTH_DOWNLOAD_DIR = Path(__file__).resolve().parents[1]
if str(AUTH_DOWNLOAD_DIR) not in sys.path:
    sys.path.insert(0, str(AUTH_DOWNLOAD_DIR))

import v5_device_dna_register as register
import device_dna_register_auth as register_auth


DNA = "0123456789ABCDEF"
PUBLIC_KEY_PEM = "-----BEGIN PUBLIC KEY-----\nfocused-test\n-----END PUBLIC KEY-----\n"
PUBLIC_KEY_SHA256 = "a" * 64


class _Response:
    def __init__(self, payload: dict[str, object]):
        self._payload = json.dumps(payload).encode("utf-8")

    def __enter__(self) -> "_Response":
        return self

    def __exit__(self, *_args: object) -> None:
        return None

    def read(self) -> bytes:
        return self._payload


class DeviceDnaRegisterContractTest(unittest.TestCase):
    def setUp(self) -> None:
        self.args = register.parse_args(["--server-url", "https://vps.example.test"])
        self.dna_report = {
            "value": DNA,
            "source": "/dev/v5-dna-uio",
            "type": "zynq7000_pl_device_dna_57",
        }
        self.key_info = {
            "public_key_pem": PUBLIC_KEY_PEM,
            "public_key_sha256": PUBLIC_KEY_SHA256,
        }

    def _registration_patches(self, response: dict[str, object]):
        return (
            mock.patch.object(register, "read_live_dna", return_value=self.dna_report),
            mock.patch.object(register, "prepare_device_keypair", return_value=self.key_info),
            mock.patch.object(register, "http_post_json", return_value=response),
            mock.patch.object(register, "atomic_write_json"),
            mock.patch.object(register, "write_device_auth_latch"),
        )

    def test_http_request_has_public_key_fields_and_no_bearer_token(self) -> None:
        captured: dict[str, object] = {}

        def urlopen(request: object, timeout: float):
            captured["request"] = request
            captured["timeout"] = timeout
            return _Response({"success": True, "vpsDistributionId": "123456"})

        payload = {
            "device_public_key_pem": PUBLIC_KEY_PEM,
            "device_public_key_sha256": PUBLIC_KEY_SHA256,
        }
        with mock.patch.object(register.urllib.request, "urlopen", side_effect=urlopen):
            result = register.http_post_json(
                "https://vps.example.test/api/v1/factory/devices/register-dna",
                3.0,
                payload,
                DNA,
            )

        request = captured["request"]
        self.assertIsNone(request.get_header("Authorization"))
        self.assertEqual(request.get_header("X-8ax-device-dna"), DNA)
        request_payload = json.loads(request.data.decode("utf-8"))
        self.assertEqual(request_payload["device_public_key_pem"], PUBLIC_KEY_PEM)
        self.assertEqual(request_payload["device_public_key_sha256"], PUBLIC_KEY_SHA256)
        self.assertEqual(result["vpsDistributionId"], "123456")

    def test_same_dna_preserves_existing_canonical_vps_id(self) -> None:
        response = {
            "success": True,
            "vpsDistributionId": "654321",
            "plDnaHash": "c" * 64,
            "deviceDna": DNA,
        }
        patches = self._registration_patches(response)
        with patches[0], patches[1], patches[2], patches[3], patches[4]:
            first = register.register_device_dna(self.args)
            second = register.register_device_dna(self.args)

        self.assertTrue(first["ok"])
        self.assertEqual(first["code"], "DNA_REGISTER_UPLOADED_PENDING_AUTH")
        self.assertEqual(first["vpsDistributionId"], "654321")
        self.assertEqual(second["vpsDistributionId"], "654321")
        self.assertNotIn("plDnaHash", first["server"])
        self.assertNotIn("deviceDna", first["server"])

    def test_invalid_public_key_rejection_is_fail_closed(self) -> None:
        response = {"success": False, "code": "PUBLIC_KEY_INVALID", "message": "device public key invalid"}
        cached_identity = mock.patch.object(register, "require_registered_identity")
        patches = self._registration_patches(response)
        with cached_identity as require_cached, patches[0], patches[1], patches[2], patches[3], patches[4]:
            result = register.register_device_dna(self.args)

        self.assertFalse(result["ok"])
        self.assertEqual(result["code"], "DNA_REGISTER_FAILED")
        require_cached.assert_not_called()

    def test_missing_or_invalid_canonical_id_is_fail_closed(self) -> None:
        for response in (
            {"success": True},
            {"success": True, "vpsDistributionId": "12345"},
            {"success": True, "vpsDistributionId": "ABC123"},
        ):
            with self.subTest(response=response):
                cached_identity = mock.patch.object(register, "require_registered_identity")
                patches = self._registration_patches(response)
                with cached_identity as require_cached, patches[0], patches[1], patches[2], patches[3], patches[4]:
                    result = register.register_device_dna(self.args)
                self.assertFalse(result["ok"])
                self.assertEqual(result["code"], "DNA_REGISTER_FAILED")
                require_cached.assert_not_called()

    def test_invalid_partial_key_is_replaced_only_after_valid_generation(self) -> None:
        with tempfile.TemporaryDirectory() as raw_root:
            root = Path(raw_root)
            private_key = root / "device_private_key.pem"
            public_key = root / "device_public_key.pem"
            private_key.write_bytes(b"partial-key")
            args = SimpleNamespace(
                device_private_key_file=str(private_key),
                device_public_key_file=str(public_key),
            )
            calls: list[list[str]] = []

            def run_openssl(command: list[str], **kwargs: object) -> SimpleNamespace:
                calls.append(command)
                if command[1] == "rsa" and command[-1] == str(private_key):
                    return SimpleNamespace(returncode=1, stdout=b"", stderr=b"invalid")
                if command[1] == "genpkey":
                    Path(command[-1]).write_bytes(b"valid-private-key")
                    self.assertEqual(
                        kwargs["timeout"],
                        register_auth.DEVICE_PRIVATE_KEY_GENERATION_TIMEOUT_S)
                    return SimpleNamespace(returncode=0, stdout=b"", stderr=b"")
                return SimpleNamespace(
                    returncode=0,
                    stdout=b"-----BEGIN PUBLIC KEY-----\nvalid\n-----END PUBLIC KEY-----\n",
                    stderr=b"",
                )

            with mock.patch.object(
                    register_auth.subprocess, "run", side_effect=run_openssl):
                result = register_auth.prepare_device_keypair(args, create=True)

            self.assertEqual(private_key.read_bytes(), b"valid-private-key")
            self.assertIn("BEGIN PUBLIC KEY", result["public_key_pem"])
            self.assertEqual(len(calls), 3)
            self.assertEqual(list(root.glob("*.keygen.tmp")), [])

    def test_key_generation_timeout_preserves_existing_partial_key(self) -> None:
        with tempfile.TemporaryDirectory() as raw_root:
            root = Path(raw_root)
            private_key = root / "device_private_key.pem"
            public_key = root / "device_public_key.pem"
            private_key.write_bytes(b"partial-key")
            args = SimpleNamespace(
                device_private_key_file=str(private_key),
                device_public_key_file=str(public_key),
            )

            def run_openssl(command: list[str], **_kwargs: object) -> SimpleNamespace:
                if command[1] == "rsa":
                    return SimpleNamespace(returncode=1, stdout=b"", stderr=b"invalid")
                raise subprocess.TimeoutExpired(command, 180.0)

            with mock.patch.object(
                    register_auth.subprocess, "run", side_effect=run_openssl):
                with self.assertRaises(register_auth.DnaRegisterError) as raised:
                    register_auth.prepare_device_keypair(args, create=True)

            self.assertEqual(
                raised.exception.code,
                "DEVICE_PRIVATE_KEY_CREATE_TIMEOUT")
            self.assertEqual(private_key.read_bytes(), b"partial-key")
            self.assertFalse(public_key.exists())
            self.assertEqual(list(root.glob("*.keygen.tmp")), [])


if __name__ == "__main__":
    unittest.main()
