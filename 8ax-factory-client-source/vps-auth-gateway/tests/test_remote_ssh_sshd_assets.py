from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class RemoteSshSshdAssetsTests(unittest.TestCase):
    def test_tunnel_user_uses_isolated_authorized_keys_command(self):
        config = (ROOT / "sshd_8ax_remote_ssh.conf").read_text(encoding="utf-8")
        self.assertIn("Match User 8ax-tunnel", config)
        self.assertIn("AuthorizedKeysFile none", config)
        self.assertIn(
            "AuthorizedKeysCommand /usr/local/libexec/8ax-remote-ssh-authorized-keys %u",
            config,
        )
        self.assertIn("AuthorizedKeysCommandUser z20auth", config)
        self.assertNotIn("StrictModes no", config)

    def test_helper_only_serves_the_tunnel_user(self):
        helper = (ROOT / "remote_ssh_authorized_keys.sh").read_text(encoding="utf-8")
        self.assertIn('[ "$1" = "8ax-tunnel" ] || exit 1', helper)
        self.assertIn("/opt/8ax-auth/storage/remote-ssh/authorized_keys", helper)


if __name__ == "__main__":
    unittest.main()
