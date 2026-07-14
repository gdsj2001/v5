"""VPS owner for per-device reverse SSH tunnel registration and status."""

import hashlib
import json
import os
import re
import socket
import subprocess
import tempfile
import threading
from pathlib import Path


REMOTE_SSH_HOST = os.environ.get("AX8_REMOTE_SSH_HOST", "it.cjwsjzyy.xyz").strip()
REMOTE_SSH_PORT = int(os.environ.get("AX8_REMOTE_SSH_PORT", "22"))
REMOTE_SSH_USER = os.environ.get("AX8_REMOTE_SSH_USER", "8ax-tunnel").strip()
REMOTE_PORT_MIN = int(os.environ.get("AX8_REMOTE_SSH_PORT_MIN", "25000"))
REMOTE_PORT_MAX = int(os.environ.get("AX8_REMOTE_SSH_PORT_MAX", "44999"))
AUTHORIZED_KEYS_FILE = Path(
    os.environ.get(
        "AX8_REMOTE_SSH_AUTHORIZED_KEYS_FILE",
        "/opt/8ax-auth/storage/remote-ssh/authorized_keys",
    )
)

_DEVICE_ID_RE = re.compile(r"^[0-9]{6}$")
_SSH_KEY_RE = re.compile(r"^(ssh-rsa|ssh-ed25519) ([A-Za-z0-9+/]+={0,3})(?: .*)?$")
_SCHEMA_READY = False
_SYNC_LOCK = threading.Lock()


class RemoteSshError(RuntimeError):
    pass


def _last_json(result):
    if result.returncode != 0:
        raise RemoteSshError((result.stderr or result.stdout or "database operation failed").strip())
    lines = (result.stdout or "").strip().splitlines()
    if not lines:
        return None
    try:
        return json.loads(lines[-1])
    except Exception as exc:
        raise RemoteSshError("remote SSH database response is invalid") from exc


def ensure_schema(psql):
    global _SCHEMA_READY
    if _SCHEMA_READY:
        return
    sql = """
CREATE TABLE IF NOT EXISTS remote_ssh_tunnels (
  device_id text PRIMARY KEY REFERENCES devices(device_id) ON DELETE CASCADE,
  assigned_port integer NOT NULL UNIQUE CHECK (assigned_port BETWEEN 25000 AND 44999),
  ssh_public_key text NOT NULL,
  device_public_key_sha256 text NOT NULL,
  last_request_ip inet,
  registered_at timestamptz NOT NULL DEFAULT now(),
  updated_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_remote_ssh_tunnels_port ON remote_ssh_tunnels(assigned_port);
SELECT json_build_object('ok', true)::text;
"""
    row = _last_json(psql(sql, {}))
    if not isinstance(row, dict) or not row.get("ok"):
        raise RemoteSshError("remote SSH schema is unavailable")
    _SCHEMA_READY = True


def pem_to_openssh(public_key_pem):
    raw = str(public_key_pem or "").strip()
    if "BEGIN PUBLIC KEY" not in raw or "PRIVATE KEY" in raw:
        raise RemoteSshError("registered device public key is invalid")
    with tempfile.TemporaryDirectory(prefix="8ax-remote-ssh-") as temp_dir:
        public_path = Path(temp_dir) / "device_public.pem"
        public_path.write_text(raw + "\n", encoding="ascii")
        result = subprocess.run(
            ["/usr/bin/ssh-keygen", "-i", "-m", "PKCS8", "-f", str(public_path)],
            text=True,
            capture_output=True,
            timeout=10,
        )
    if result.returncode != 0:
        raise RemoteSshError("registered device public key cannot be converted for SSH")
    key = (result.stdout or "").strip()
    match = _SSH_KEY_RE.fullmatch(key)
    if not match:
        raise RemoteSshError("converted device SSH public key is invalid")
    return "%s %s" % (match.group(1), match.group(2))


def authorized_key_line(device_id, assigned_port, ssh_public_key):
    if not _DEVICE_ID_RE.fullmatch(str(device_id or "")):
        raise RemoteSshError("device id must be 6 digits")
    port = int(assigned_port)
    if port < REMOTE_PORT_MIN or port > REMOTE_PORT_MAX:
        raise RemoteSshError("assigned port is outside the remote SSH range")
    match = _SSH_KEY_RE.fullmatch(str(ssh_public_key or "").strip())
    if not match:
        raise RemoteSshError("device SSH public key is invalid")
    key = "%s %s" % (match.group(1), match.group(2))
    options = (
        'restrict,port-forwarding,permitlisten="127.0.0.1:%d",'
        'command="/bin/false"' % port
    )
    return "%s %s 8ax-device-%s" % (options, key, device_id)


def sync_authorized_keys(psql, destination=AUTHORIZED_KEYS_FILE):
    ensure_schema(psql)
    sql = """
SELECT COALESCE(json_agg(json_build_object(
  'deviceId', t.device_id,
  'assignedPort', t.assigned_port,
  'sshPublicKey', t.ssh_public_key
) ORDER BY t.device_id), '[]'::json)::text
FROM remote_ssh_tunnels t
JOIN devices d ON d.device_id = t.device_id
WHERE d.device_auth_revoked_at IS NULL
  AND lower(COALESCE(d.device_public_key_sha256, '')) = lower(t.device_public_key_sha256);
"""
    rows = _last_json(psql(sql, {}))
    if not isinstance(rows, list):
        raise RemoteSshError("remote SSH authorized key inventory is invalid")
    lines = [
        authorized_key_line(row.get("deviceId"), row.get("assignedPort"), row.get("sshPublicKey"))
        for row in rows
    ]
    target = Path(destination)
    with _SYNC_LOCK:
        target.parent.mkdir(parents=True, exist_ok=True)
        temp = target.with_name(target.name + ".tmp")
        temp.write_text("\n".join(lines) + ("\n" if lines else ""), encoding="ascii")
        os.chmod(str(temp), 0o644)
        os.replace(str(temp), str(target))
    return len(lines)


def register_tunnel(psql, device_id, public_key_pem, public_key_sha256, request_ip):
    device_id = str(device_id or "").strip()
    public_key_sha256 = str(public_key_sha256 or "").strip().lower()
    if not _DEVICE_ID_RE.fullmatch(device_id):
        raise RemoteSshError("device id must be 6 digits")
    if not re.fullmatch(r"[0-9a-f]{64}", public_key_sha256):
        raise RemoteSshError("device public key fingerprint is invalid")
    ensure_schema(psql)
    ssh_public_key = pem_to_openssh(public_key_pem)
    sql = """
WITH held AS (
  SELECT pg_advisory_xact_lock(84587211)
), existing AS (
  SELECT assigned_port FROM remote_ssh_tunnels WHERE device_id = :'device_id'
), candidate AS (
  SELECT port
  FROM held, generate_series(:'port_min'::integer, :'port_max'::integer) AS port
  WHERE NOT EXISTS (SELECT 1 FROM remote_ssh_tunnels t WHERE t.assigned_port = port)
  ORDER BY port
  LIMIT 1
), chosen AS (
  SELECT COALESCE((SELECT assigned_port FROM existing), (SELECT port FROM candidate)) AS assigned_port
), upserted AS (
  INSERT INTO remote_ssh_tunnels(
    device_id, assigned_port, ssh_public_key, device_public_key_sha256, last_request_ip
  )
  SELECT :'device_id', assigned_port, :'ssh_public_key', :'public_key_sha256', NULLIF(:'request_ip', '')::inet
  FROM chosen
  WHERE assigned_port IS NOT NULL
  ON CONFLICT(device_id) DO UPDATE SET
    ssh_public_key = EXCLUDED.ssh_public_key,
    device_public_key_sha256 = EXCLUDED.device_public_key_sha256,
    last_request_ip = EXCLUDED.last_request_ip,
    updated_at = now()
  RETURNING device_id, assigned_port, registered_at, updated_at
)
SELECT COALESCE((SELECT row_to_json(upserted)::text FROM upserted), '') AS payload;
"""
    row = _last_json(psql(sql, {
        "device_id": device_id,
        "port_min": REMOTE_PORT_MIN,
        "port_max": REMOTE_PORT_MAX,
        "ssh_public_key": ssh_public_key,
        "public_key_sha256": public_key_sha256,
        "request_ip": str(request_ip or ""),
    }))
    if not isinstance(row, dict) or not row.get("assigned_port"):
        raise RemoteSshError("no remote SSH tunnel port is available")
    sync_authorized_keys(psql)
    return {
        "deviceId": device_id,
        "assignedPort": int(row["assigned_port"]),
        "vpsHost": REMOTE_SSH_HOST,
        "vpsPort": REMOTE_SSH_PORT,
        "tunnelUser": REMOTE_SSH_USER,
    }


def _probe_ssh_banner(port, timeout=0.75):
    try:
        with socket.create_connection(("127.0.0.1", int(port)), timeout=timeout) as conn:
            conn.settimeout(timeout)
            return conn.recv(128).startswith(b"SSH-")
    except OSError:
        return False


def tunnel_status(psql, device_id):
    device_id = str(device_id or "").strip()
    if not _DEVICE_ID_RE.fullmatch(device_id):
        raise RemoteSshError("device id must be 6 digits")
    ensure_schema(psql)
    sql = """
SELECT COALESCE((
  SELECT json_build_object(
    'deviceId', t.device_id,
    'assignedPort', t.assigned_port,
    'updatedAt', t.updated_at
  )::text
  FROM remote_ssh_tunnels t
  JOIN devices d ON d.device_id = t.device_id
  WHERE t.device_id = :'device_id'
    AND d.device_auth_revoked_at IS NULL
    AND lower(COALESCE(d.device_public_key_sha256, '')) = lower(t.device_public_key_sha256)
  LIMIT 1
), '') AS payload;
"""
    row = _last_json(psql(sql, {"device_id": device_id}))
    if not isinstance(row, dict):
        return {
            "success": True,
            "deviceId": device_id,
            "registered": False,
            "online": False,
            "message": "设备尚未登记远程 SSH 隧道。",
        }
    port = int(row["assignedPort"])
    online = _probe_ssh_banner(port)
    return {
        "success": True,
        "deviceId": device_id,
        "registered": True,
        "online": online,
        "assignedPort": port,
        "vpsHost": REMOTE_SSH_HOST,
        "vpsPort": REMOTE_SSH_PORT,
        "tunnelUser": REMOTE_SSH_USER,
        "updatedAt": row.get("updatedAt"),
        "message": "远程 SSH 通道在线。" if online else "设备反向 SSH 通道未在线。",
    }


def public_key_fingerprint(ssh_public_key):
    match = _SSH_KEY_RE.fullmatch(str(ssh_public_key or "").strip())
    if not match:
        raise RemoteSshError("device SSH public key is invalid")
    return hashlib.sha256(match.group(2).encode("ascii")).hexdigest()
