from __future__ import annotations

import json
from pathlib import Path
import sys
import unittest
from unittest import mock


AUTH_DOWNLOAD_DIR = Path(__file__).resolve().parents[1]
if str(AUTH_DOWNLOAD_DIR) not in sys.path:
    sys.path.insert(0, str(AUTH_DOWNLOAD_DIR))

import v5_device_dna_register as register


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


if __name__ == "__main__":
    unittest.main()
