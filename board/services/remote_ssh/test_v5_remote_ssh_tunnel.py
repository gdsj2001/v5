import importlib.util
import tempfile
import unittest
from pathlib import Path
from unittest import mock


MODULE_PATH = Path(__file__).with_name("v5_remote_ssh_tunnel.py")
SPEC = importlib.util.spec_from_file_location("v5_remote_ssh_tunnel", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


class RemoteSshTunnelTests(unittest.TestCase):
    def test_run_forces_ipv4_for_https_registration(self):
        args = MODULE.parse_args([])
        old_stop_requested = MODULE.STOP_REQUESTED
        MODULE.STOP_REQUESTED = True
        try:
            with mock.patch.object(MODULE, "install_ipv4_urlopen_resolution") as install_ipv4, \
                    mock.patch.object(MODULE, "write_status"):
                self.assertEqual(0, MODULE.run(args))
                install_ipv4.assert_called_once_with()
        finally:
            MODULE.STOP_REQUESTED = old_stop_requested

    def test_endpoint_keeps_identity_separate_from_raw_connect_host(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            config = Path(temp_dir) / "vps_endpoints.json"
            config.write_text(
                '{"api_base_urls":["https://license.cjwsjzyy.xyz"],'
                '"remote_ssh":{"host":"it.cjwsjzyy.xyz",'
                '"connect_host":"154.21.81.167","port":22,"user":"8ax-tunnel"}}',
                encoding="utf-8",
            )
            api_urls, endpoint = MODULE.load_endpoints(str(config))
        self.assertEqual(["https://license.cjwsjzyy.xyz"], api_urls)
        self.assertEqual("it.cjwsjzyy.xyz", endpoint["host"])
        self.assertEqual("154.21.81.167", endpoint["connect_host"])

    def test_registration_requires_bound_identity(self):
        endpoint = {
            "host": "it.cjwsjzyy.xyz",
            "connect_host": "154.21.81.167",
            "port": 22,
            "user": "8ax-tunnel",
        }
        result = MODULE.registration_from_response({
            "success": True,
            "deviceId": "390529",
            "assignedPort": 25001,
            "vpsHost": endpoint["host"],
            "vpsPort": 22,
            "tunnelUser": endpoint["user"],
        }, "390529", endpoint)
        self.assertEqual(25001, result["assigned_port"])
        with self.assertRaises(MODULE.RemoteSshTunnelError):
            MODULE.registration_from_response({
                "success": True,
                "deviceId": "999999",
                "assignedPort": 25001,
                "vpsHost": endpoint["host"],
                "vpsPort": 22,
                "tunnelUser": endpoint["user"],
            }, "390529", endpoint)

    def test_ssh_command_is_loopback_only_and_pins_host_key(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            known_hosts = Path(temp_dir) / "known_hosts"
            private_key = Path(temp_dir) / "device.pem"
            known_hosts.write_text("host ssh-ed25519 key\n", encoding="ascii")
            private_key.write_text("private\n", encoding="ascii")
            args = MODULE.parse_args([
                "--known-hosts-file", str(known_hosts),
                "--device-private-key-file", str(private_key),
            ])
            command = MODULE.build_ssh_command(args, {
                "host": "it.cjwsjzyy.xyz",
                "connect_host": "154.21.81.167",
                "port": 22,
                "user": "8ax-tunnel",
                "assigned_port": 25001,
            })
        self.assertIn("127.0.0.1:25001:127.0.0.1:22", command)
        self.assertIn("StrictHostKeyChecking=yes", command)
        self.assertIn("HostKeyAlias=it.cjwsjzyy.xyz", command)
        self.assertIn("UserKnownHostsFile=" + str(known_hosts), command)
        self.assertEqual("8ax-tunnel@154.21.81.167", command[-1])
        self.assertNotIn("0.0.0.0:25001:127.0.0.1:22", command)

    def test_permission_is_explicit(self):
        MODULE.require_remote_permission({"permissions": ["drive_profile_download", "remote_ssh_tunnel"]})
        with self.assertRaises(MODULE.RemoteSshTunnelError):
            MODULE.require_remote_permission({"permissions": ["drive_profile_download"]})


if __name__ == "__main__":
    unittest.main()
