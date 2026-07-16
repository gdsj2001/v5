import importlib.util
from pathlib import Path
from types import SimpleNamespace
import unittest


MODULE_PATH = Path(__file__).resolve().parents[1] / "remote_ssh_gateway.py"
SPEC = importlib.util.spec_from_file_location("remote_ssh_gateway", MODULE_PATH)
remote = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(remote)


class RemoteSshGatewayTests(unittest.TestCase):
    def test_psql_error_on_stderr_is_not_treated_as_success(self):
        result = SimpleNamespace(
            returncode=0,
            stdout='{"ok":true}\n',
            stderr='ERROR: permission denied for table devices\n',
        )
        with self.assertRaises(remote.RemoteSshError):
            remote._last_json(result)

    def test_schema_does_not_require_references_on_existing_devices_table(self):
        captured = {}

        def psql(sql, variables):
            captured["sql"] = sql
            captured["variables"] = variables
            return SimpleNamespace(returncode=0, stdout='{"ok":true}\n', stderr="")

        old_ready = remote._SCHEMA_READY
        remote._SCHEMA_READY = False
        try:
            remote.ensure_schema(psql)
        finally:
            remote._SCHEMA_READY = old_ready
        self.assertIn("CREATE TABLE IF NOT EXISTS remote_ssh_tunnels", captured["sql"])
        self.assertNotIn("REFERENCES devices", captured["sql"])
        self.assertEqual({}, captured["variables"])

    def test_authorized_key_is_loopback_and_device_scoped(self):
        key = "ssh-rsa " + ("A" * 128)
        line = remote.authorized_key_line("390529", 25017, key)
        self.assertIn('permitlisten="127.0.0.1:25017"', line)
        self.assertIn("restrict,port-forwarding", line)
        self.assertIn('command="/bin/false"', line)
        self.assertTrue(line.endswith("8ax-device-390529"))

    def test_authorized_key_rejects_public_listen_port(self):
        key = "ssh-rsa " + ("A" * 128)
        with self.assertRaises(remote.RemoteSshError):
            remote.authorized_key_line("390529", 22, key)

    def test_device_id_must_be_canonical(self):
        key = "ssh-rsa " + ("A" * 128)
        with self.assertRaises(remote.RemoteSshError):
            remote.authorized_key_line("device-390529", 25017, key)


if __name__ == "__main__":
    unittest.main()
