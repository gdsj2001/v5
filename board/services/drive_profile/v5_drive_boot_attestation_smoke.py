#!/usr/bin/env python3
from __future__ import annotations

import unittest
from typing import Any, Dict, List
from unittest import mock

import v5_drive_boot_attestation as attestation


def drive_apply() -> Dict[str, Any]:
    return {
        "ok": True,
        "code": "DRIVE_SET_OK",
        "batch_readback_started_monotonic_ns": 456789,
        "drive_transaction_identity": {
            "transaction_generation": "a" * 64,
            "native_mapping_generation": 222,
            "persistent_mapping_generation": 222,
            "native_mapping_matches_persistent": True,
        },
        "targets": [
            {
                "ok": True,
                "code": "DRIVE_SET_TARGET_OK",
                "status_slot": slot,
                "readback": {"health": {"ok": True}},
            }
            for slot in range(5)
        ],
    }


class DriveBootAttestationSmoke(unittest.TestCase):
    def test_full_identity_is_submitted_and_read_back(self) -> None:
        backend = {
            "generation": 111,
            "owner_pid": 2222,
            "owner_start_ticks": 3333,
            "expected_slaves": 5,
        }
        calls: List[List[str]] = []

        def probe(argv: List[str], _timeout_s: float) -> Dict[str, str]:
            calls.append(list(argv))
            return {
                "code": "BACKEND_DRIVE_VERIFIED",
                "generation": "111",
                "owner_pid": "2222",
                "owner_start_ticks": "3333",
                "data_ready": "1",
                "motion_ready": "1",
                "drive_verified": "1",
                "slaves": "5/5",
                "drive_mapping_generation": "222",
                "drive_axis_mask": "31",
                "drive_readback_count": "5",
                "drive_readback_started": "456789",
                "drive_verified_at": "456999",
                "drive_identity_sha256": "a" * 64,
            }

        with mock.patch.object(
            attestation, "backend_data_identity", return_value=backend,
        ), mock.patch.object(attestation, "_probe", side_effect=probe):
            result = attestation.attest_boot_drive_generation(drive_apply())

        self.assertTrue(result["ok"])
        self.assertTrue(result["motion_ready"])
        self.assertEqual(31, result["axis_mask"])
        self.assertEqual("--verify-drive", calls[0][0])
        self.assertIn("--drive-transaction-sha256", calls[0])

    def test_duplicate_or_missing_slot_is_rejected_before_native_submit(self) -> None:
        payload = drive_apply()
        payload["targets"][4]["status_slot"] = 3
        with mock.patch.object(
            attestation,
            "backend_data_identity",
            return_value={
                "generation": 111,
                "owner_pid": 2222,
                "owner_start_ticks": 3333,
                "expected_slaves": 5,
            },
        ), mock.patch.object(attestation, "_probe") as probe:
            with self.assertRaises(attestation.DriveAttestationError) as raised:
                attestation.attest_boot_drive_generation(payload)
        self.assertEqual(
            "DRIVE_NATIVE_ATTESTATION_STATUS_SLOT_MISMATCH",
            raised.exception.code,
        )
        probe.assert_not_called()

    def test_malformed_slave_count_is_structured_failure(self) -> None:
        fields = {
            "code": "BACKEND_DATA_READY",
            "generation": "111",
            "owner_pid": "2222",
            "owner_start_ticks": "3333",
            "data_ready": "1",
            "motion_ready": "0",
            "drive_verified": "0",
            "slaves": "bad/5",
        }
        with mock.patch.object(attestation, "_probe", return_value=fields):
            with self.assertRaises(attestation.DriveAttestationError) as raised:
                attestation.backend_data_identity(1.0)
        self.assertEqual(
            "DRIVE_NATIVE_ATTESTATION_RESPONSE_INVALID",
            raised.exception.code,
        )


if __name__ == "__main__":
    unittest.main()
