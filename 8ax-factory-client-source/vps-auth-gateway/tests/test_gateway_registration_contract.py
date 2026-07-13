from __future__ import annotations

import importlib.util
import json
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest import mock


GATEWAY_PATH = Path(__file__).resolve().parents[1] / "gateway.py"
SPEC = importlib.util.spec_from_file_location("vps_auth_gateway", GATEWAY_PATH)
gateway = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(gateway)

DNA = "0x0123456789ABCDEF"
PUBLIC_KEY_PEM = "-----BEGIN PUBLIC KEY-----\ncanonical-test-key\n-----END PUBLIC KEY-----\n"
PUBLIC_KEY_SHA256 = "a" * 64


class GatewayRegistrationContractTest(unittest.TestCase):
    def _handler(self):
        handler = object.__new__(gateway.Handler)
        handler.headers = {"X-8AX-Device-DNA": DNA}
        handler.client_address = ("127.0.0.1", 12345)
        responses: list[tuple[int, dict[str, object]]] = []
        handler._json = lambda code, data: responses.append((code, data))
        handler._ensure_device_private_layout = lambda _device_id, _dna_hash: Path("/private/current-device")
        return handler, responses

    @staticmethod
    def _body(public_key_sha256: str = PUBLIC_KEY_SHA256) -> bytes:
        return json.dumps(
            {
                "source": "re-v5-lvgl-settings",
                "device_id_source": "/dev/v5-dna-uio",
                "device_public_key_pem": PUBLIC_KEY_PEM,
                "device_public_key_sha256": public_key_sha256,
            }
        ).encode("utf-8")

    def _database_results(self, device_id: str, stored_public_key_sha256: str = ""):
        registration = {
            "success": True,
            "status": "registered",
            "deviceId": device_id,
            "vpsDistributionId": device_id,
            "plDnaHash": "c" * 64,
        }
        return [
            SimpleNamespace(returncode=0, stdout=json.dumps(registration) + "\n"),
            SimpleNamespace(returncode=0, stdout=stored_public_key_sha256 + "\n"),
            SimpleNamespace(returncode=0, stdout=device_id + "\n"),
        ]

    def test_no_token_registration_accepts_public_key_and_returns_six_digit_id(self) -> None:
        handler, responses = self._handler()
        database = self._database_results("359764")
        with (
            mock.patch.object(gateway, "_ensure_device_security_schema", return_value=True),
            mock.patch.object(
                gateway,
                "_canonical_public_key_from_request",
                return_value=(PUBLIC_KEY_PEM, PUBLIC_KEY_SHA256),
            ),
            mock.patch.object(gateway, "_allocate_pl_dna_device_id", return_value=("359764", "allocated")),
            mock.patch.object(gateway, "_psql_json", side_effect=database),
        ):
            handler._handle_factory_device_register(self._body())

        self.assertEqual(len(responses), 1)
        status, response = responses[0]
        self.assertEqual(status, 200)
        self.assertTrue(response["success"])
        self.assertEqual(response["vpsDistributionId"], "359764")
        self.assertEqual(response["devicePublicKeySha256"], PUBLIC_KEY_SHA256)
        self.assertEqual(response["authorizationStatus"], "pending_factory_upload")
        self.assertNotIn("plDnaHash", response)

    def test_same_dna_returns_saved_canonical_id(self) -> None:
        handler, responses = self._handler()
        database = self._database_results("359764", PUBLIC_KEY_SHA256)
        with (
            mock.patch.object(gateway, "_ensure_device_security_schema", return_value=True),
            mock.patch.object(
                gateway,
                "_canonical_public_key_from_request",
                return_value=(PUBLIC_KEY_PEM, PUBLIC_KEY_SHA256),
            ),
            mock.patch.object(gateway, "_allocate_pl_dna_device_id", return_value=("359764", "saved")),
            mock.patch.object(gateway, "_psql_json", side_effect=database),
        ):
            handler._handle_factory_device_register(self._body())

        self.assertEqual(responses[0][0], 200)
        self.assertEqual(responses[0][1]["vpsDistributionId"], "359764")

    def test_invalid_public_key_is_rejected_before_device_allocation(self) -> None:
        handler, responses = self._handler()
        allocate = mock.Mock()
        database = mock.Mock()
        with (
            mock.patch.object(gateway, "_ensure_device_security_schema", return_value=True),
            mock.patch.object(gateway, "_allocate_pl_dna_device_id", allocate),
            mock.patch.object(gateway, "_psql_json", database),
        ):
            handler._handle_factory_device_register(
                json.dumps({"device_public_key_pem": "not-a-public-key"}).encode("utf-8")
            )

        self.assertEqual(responses[0][0], 400)
        self.assertEqual(responses[0][1]["status"], "device_public_key_missing")
        allocate.assert_not_called()
        database.assert_not_called()

    def test_public_key_fingerprint_mismatch_is_fail_closed(self) -> None:
        handler, responses = self._handler()
        allocate = mock.Mock()
        with (
            mock.patch.object(gateway, "_ensure_device_security_schema", return_value=True),
            mock.patch.object(
                gateway,
                "_canonical_public_key_from_request",
                return_value=(PUBLIC_KEY_PEM, PUBLIC_KEY_SHA256),
            ),
            mock.patch.object(gateway, "_allocate_pl_dna_device_id", allocate),
        ):
            handler._handle_factory_device_register(self._body("b" * 64))

        self.assertEqual(responses[0][0], 400)
        self.assertEqual(responses[0][1]["status"], "device_public_key_mismatch")
        allocate.assert_not_called()


if __name__ == "__main__":
    unittest.main()
