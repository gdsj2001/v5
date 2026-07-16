#!/usr/bin/env python3
import json
import base64
import hashlib
import html
import mimetypes
import os
import re
import secrets
import socket
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timedelta, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

sys.path.insert(0, str(Path(__file__).resolve().parent))
import remote_ssh_gateway

SERVICE = "8ax-auth-gateway"
VERSION = "2026-07-14-001"
STARTED_AT = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
DEALER_WEB_ROOT = Path(os.environ.get("AX8_DEALER_WEB_ROOT", "/opt/8ax-auth/web/dealer/current"))
DEALER_CLIENT_ROOT = Path(os.environ.get("AX8_DEALER_CLIENT_ROOT", "/opt/8ax-auth/storage/dealer-client"))
DRIVE_PROFILE_ROOT = Path(os.environ.get("AX8_DRIVE_PROFILE_ROOT", "/opt/8ax-auth/storage/drive-profiles"))
DEVICE_PRIVATE_ROOT = Path(os.environ.get("AX8_DEVICE_PRIVATE_ROOT", "/opt/8ax-auth/storage/private"))
WINREMOTE_UPDATE_ROOT = Path(os.environ.get("AX8_WINREMOTE_UPDATE_ROOT", "/var/www/html/updates/8ax-winremote"))
DEVICE_AUTH_PRIVATE_KEY_FILE = Path(os.environ.get("AX8_DEVICE_AUTH_PRIVATE_KEY_FILE", "/opt/8ax-auth/secrets/device-auth-signing-private.pem"))
DEVICE_AUTH_PUBLIC_KEY_FILE = Path(os.environ.get("AX8_DEVICE_AUTH_PUBLIC_KEY_FILE", "/opt/8ax-auth/public/device-auth-signing-public.pem"))
DEVICE_AUTH_SCHEMA = "8ax-device-authorization-v1"
DEVICE_AUTH_ENVELOPE_SCHEMA = "8ax-device-authorization-envelope-v1"
DEVICE_AUTH_SIGNATURE_ALG = "RSASSA-PKCS1-v1_5-SHA256"
DEVICE_REQUEST_SIGNATURE_SCHEMA = "8ax-device-request-signature-v1"
DEVICE_REQUEST_SIGNATURE_ALG = "RSASSA-PKCS1-v1_5-SHA256"
DEVICE_AUTH_DEFAULT_DAYS = int(os.environ.get("AX8_DEVICE_AUTH_DEFAULT_DAYS", "30"))
LATEST_CLIENT_VERSION = "0.1.15"
LATEST_CLIENT_BUILD = "2026.04.29.010"
LATEST_CLIENT_FILE = "6x-cnc.DealerClient-0.1.15-win-x64.exe"
LATEST_CLIENT_SHA256 = "C8D9DC9B01F9CFA2CE4FA3709FBF81490213190EFA0B34F7AF21072D02AA4156"
LATEST_CLIENT_SIZE = 50429126



def _load_db_env():
    env_path = Path('/opt/8ax-auth/secrets/postgres-8ax-auth.env')
    values = {}
    if env_path.exists():
        for line in env_path.read_text(encoding='utf-8').splitlines():
            line = line.strip()
            if not line or line.startswith('#') or '=' not in line:
                continue
            key, value = line.split('=', 1)
            values[key.strip().lstrip('\ufeff')] = value.strip().strip('"').strip("'")
    return values

def _load_admin_env():
    env_path = Path('/opt/8ax-auth/secrets/admin-review.env')
    values = {}
    if env_path.exists():
        for line in env_path.read_text(encoding='utf-8').splitlines():
            line = line.strip()
            if not line or line.startswith('#') or '=' not in line:
                continue
            key, value = line.split('=', 1)
            values[key.strip()] = value.strip().strip('"').strip("'")
    return values

def _redact_access_log_message(message):
    return re.sub(r"(?i)(device_dna=)[^&\s\"]+", r"\1<redacted>", message)

def _normalize_pl_dna(value):
    text = str(value or "").strip()
    if text.lower().startswith("0x"):
        text = text[2:]
    if not re.fullmatch(r"[0-9a-fA-F]{16}", text):
        return ""
    number = int(text, 16)
    if number >= (1 << 57):
        return ""
    return "0x%016X" % number

def _pl_dna_hash(value):
    return hashlib.sha256(str(value or "").strip().upper().encode("ascii", errors="ignore")).hexdigest()


def _pl_dna_short_device_id(value, attempt=0):
    normalized = str(value or "").strip().upper()
    material = normalized if attempt <= 0 else f"{normalized}:vps-distribution-id:{attempt}"
    digest = hashlib.sha256(material.encode("ascii", errors="ignore")).digest()
    return "%06d" % (int.from_bytes(digest[:8], "big") % 1000000)


def _b64url_encode(data):
    return base64.urlsafe_b64encode(data).decode("ascii").rstrip("=")


def _b64url_decode(text):
    raw = str(text or "").strip()
    if not raw:
        return b""
    raw += "=" * ((4 - len(raw) % 4) % 4)
    return base64.urlsafe_b64decode(raw.encode("ascii"))


def _canonical_json_bytes(data):
    return json.dumps(data, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")


def _utc_now():
    return datetime.now(timezone.utc).replace(microsecond=0)


def _iso_utc(value):
    return value.astimezone(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def _parse_iso_utc(value):
    text = str(value or "").strip()
    if not text:
        return None
    if text.endswith("Z"):
        text = text[:-1] + "+00:00"
    try:
        parsed = datetime.fromisoformat(text)
    except Exception:
        return None
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=timezone.utc)
    return parsed.astimezone(timezone.utc)


def _openssl_path():
    return os.environ.get("AX8_OPENSSL", "").strip() or "/usr/bin/openssl"


def _ensure_device_auth_keypair():
    openssl = _openssl_path()
    DEVICE_AUTH_PRIVATE_KEY_FILE.parent.mkdir(parents=True, exist_ok=True)
    DEVICE_AUTH_PUBLIC_KEY_FILE.parent.mkdir(parents=True, exist_ok=True)
    if not DEVICE_AUTH_PRIVATE_KEY_FILE.exists():
        result = subprocess.run(
            [openssl, "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:3072", "-out", str(DEVICE_AUTH_PRIVATE_KEY_FILE)],
            text=True,
            capture_output=True,
            timeout=20,
        )
        if result.returncode != 0:
            raise RuntimeError("device auth private key generation failed")
        try:
            os.chmod(str(DEVICE_AUTH_PRIVATE_KEY_FILE), 0o600)
        except Exception:
            pass
    if not DEVICE_AUTH_PUBLIC_KEY_FILE.exists():
        result = subprocess.run(
            [openssl, "rsa", "-pubout", "-in", str(DEVICE_AUTH_PRIVATE_KEY_FILE), "-out", str(DEVICE_AUTH_PUBLIC_KEY_FILE)],
            text=True,
            capture_output=True,
            timeout=10,
        )
        if result.returncode != 0:
            raise RuntimeError("device auth public key export failed")
        try:
            os.chmod(str(DEVICE_AUTH_PUBLIC_KEY_FILE), 0o644)
        except Exception:
            pass


def _device_auth_key_id():
    override = os.environ.get("AX8_DEVICE_AUTH_KEY_ID", "").strip()
    if override:
        return override
    _ensure_device_auth_keypair()
    digest = hashlib.sha256(DEVICE_AUTH_PUBLIC_KEY_FILE.read_bytes()).hexdigest()
    return "rsa-sha256:" + digest[:16]


def _openssl_sign(payload_bytes):
    _ensure_device_auth_keypair()
    result = subprocess.run(
        [_openssl_path(), "dgst", "-sha256", "-sign", str(DEVICE_AUTH_PRIVATE_KEY_FILE)],
        input=payload_bytes,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10,
    )
    if result.returncode != 0 or not result.stdout:
        raise RuntimeError("device authorization signing failed")
    return result.stdout


def _openssl_verify(payload_bytes, signature_bytes):
    _ensure_device_auth_keypair()
    tmp_name = ""
    try:
        with tempfile.NamedTemporaryFile(prefix="8ax-device-auth-sig.", delete=False) as fh:
            fh.write(signature_bytes)
            tmp_name = fh.name
        result = subprocess.run(
            [_openssl_path(), "dgst", "-sha256", "-verify", str(DEVICE_AUTH_PUBLIC_KEY_FILE), "-signature", tmp_name],
            input=payload_bytes,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=10,
        )
        return result.returncode == 0
    finally:
        if tmp_name:
            try:
                os.unlink(tmp_name)
            except FileNotFoundError:
                pass


def _normalize_public_key_pem(data):
    raw = data if isinstance(data, bytes) else str(data or "").encode("utf-8", errors="ignore")
    return raw.replace(b"\r\n", b"\n").replace(b"\r", b"\n").strip() + b"\n"


def _public_key_sha256(public_key_pem):
    return hashlib.sha256(_normalize_public_key_pem(public_key_pem)).hexdigest()


def _canonical_public_key_from_request(value):
    text = str(value or "").strip()
    if "BEGIN PUBLIC KEY" not in text:
        return "", ""
    with tempfile.NamedTemporaryFile(prefix="8ax-device-pub-in.", delete=False) as inp:
        inp.write(text.encode("utf-8"))
        inp_name = inp.name
    try:
        result = subprocess.run(
            [_openssl_path(), "pkey", "-pubin", "-in", inp_name, "-pubout"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=10,
        )
        if result.returncode != 0 or not result.stdout:
            return "", ""
        public_pem = _normalize_public_key_pem(result.stdout).decode("ascii", errors="ignore")
        return public_pem, _public_key_sha256(public_pem.encode("ascii", errors="ignore"))
    finally:
        try:
            os.unlink(inp_name)
        except FileNotFoundError:
            pass


def _openssl_verify_with_public_key(public_key_pem, payload_bytes, signature_bytes):
    tmp_pub = ""
    tmp_sig = ""
    try:
        with tempfile.NamedTemporaryFile(prefix="8ax-device-pub.", delete=False) as fh:
            fh.write(_normalize_public_key_pem(public_key_pem))
            tmp_pub = fh.name
        with tempfile.NamedTemporaryFile(prefix="8ax-device-req-sig.", delete=False) as fh:
            fh.write(signature_bytes)
            tmp_sig = fh.name
        result = subprocess.run(
            [_openssl_path(), "dgst", "-sha256", "-verify", tmp_pub, "-signature", tmp_sig],
            input=payload_bytes,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=10,
        )
        return result.returncode == 0
    finally:
        for name in (tmp_pub, tmp_sig):
            if name:
                try:
                    os.unlink(name)
                except FileNotFoundError:
                    pass


_DEVICE_SECURITY_SCHEMA_READY = False


def _ensure_device_security_schema():
    global _DEVICE_SECURITY_SCHEMA_READY
    if _DEVICE_SECURITY_SCHEMA_READY:
        return True
    sql = """
ALTER TABLE devices ADD COLUMN IF NOT EXISTS device_public_key_pem text;
ALTER TABLE devices ADD COLUMN IF NOT EXISTS device_public_key_sha256 text;
ALTER TABLE devices ADD COLUMN IF NOT EXISTS device_public_key_registered_at timestamptz;
ALTER TABLE devices ADD COLUMN IF NOT EXISTS device_auth_revoked_at timestamptz;
ALTER TABLE devices ADD COLUMN IF NOT EXISTS device_auth_revocation_reason text;
CREATE TABLE IF NOT EXISTS device_challenges (
  nonce text PRIMARY KEY,
  device_id text NOT NULL,
  purpose text NOT NULL,
  request_ip inet,
  created_at timestamptz NOT NULL DEFAULT now(),
  expires_at timestamptz NOT NULL,
  used_at timestamptz,
  used_path text
);
ALTER TABLE device_challenges ADD COLUMN IF NOT EXISTS used_request_ip inet;
CREATE INDEX IF NOT EXISTS idx_device_challenges_device_created ON device_challenges(device_id, created_at DESC);
CREATE INDEX IF NOT EXISTS idx_device_challenges_expiry ON device_challenges(expires_at);
"""
    result = _psql_json(sql, {})
    if result.returncode == 0:
        _DEVICE_SECURITY_SCHEMA_READY = True
        return True
    print("device security schema migration failed: %s" % (result.stderr or result.stdout or ""), flush=True)
    return False


def _sign_device_authorization(payload_data):
    key_id = _device_auth_key_id()
    payload_data = dict(payload_data)
    payload_data["schema"] = DEVICE_AUTH_SCHEMA
    payload_data["signature_alg"] = DEVICE_AUTH_SIGNATURE_ALG
    payload_data["key_id"] = key_id
    signature = _openssl_sign(_canonical_json_bytes(payload_data))
    return {
        "schema": DEVICE_AUTH_ENVELOPE_SCHEMA,
        "payload": payload_data,
        "signature": {
            "alg": DEVICE_AUTH_SIGNATURE_ALG,
            "key_id": key_id,
            "value": _b64url_encode(signature),
        },
    }


def _verify_device_authorization(envelope, dna):
    if not isinstance(envelope, dict):
        return False, "device_authorization_invalid", "device authorization file is not a JSON object", {}
    if envelope.get("schema") != DEVICE_AUTH_ENVELOPE_SCHEMA:
        return False, "device_authorization_invalid", "device authorization schema is invalid", {}
    payload_data = envelope.get("payload")
    signature = envelope.get("signature")
    if not isinstance(payload_data, dict) or not isinstance(signature, dict):
        return False, "device_authorization_invalid", "device authorization payload or signature is missing", {}
    if payload_data.get("schema") != DEVICE_AUTH_SCHEMA:
        return False, "device_authorization_invalid", "device authorization payload schema is invalid", {}
    if payload_data.get("signature_alg") != DEVICE_AUTH_SIGNATURE_ALG or signature.get("alg") != DEVICE_AUTH_SIGNATURE_ALG:
        return False, "device_authorization_invalid", "device authorization signature algorithm is invalid", {}
    if str(payload_data.get("key_id", "")) != str(signature.get("key_id", "")):
        return False, "device_authorization_invalid", "device authorization key id mismatch", {}
    try:
        signature_bytes = _b64url_decode(signature.get("value", ""))
    except Exception:
        return False, "device_authorization_invalid", "device authorization signature is not valid base64url", {}
    if not signature_bytes or not _openssl_verify(_canonical_json_bytes(payload_data), signature_bytes):
        return False, "device_authorization_invalid", "device authorization signature verification failed", {}
    if str(payload_data.get("pl_device_dna_hash", "")).lower() != _pl_dna_hash(dna).lower():
        return False, "device_authorization_invalid", "device authorization does not match live DNA", {}
    if not str(payload_data.get("device_public_key_sha256", "")).strip():
        return False, "device_public_key_missing", "device authorization does not bind a device public key", {}
    permissions = payload_data.get("permissions")
    if not isinstance(permissions, list) or "drive_profile_download" not in [str(item) for item in permissions]:
        return False, "device_authorization_invalid", "device authorization does not permit drive profile download", {}
    now = _utc_now()
    not_before = _parse_iso_utc(payload_data.get("not_before"))
    expires_at = _parse_iso_utc(payload_data.get("expires_at"))
    if not_before is None:
        return False, "device_authorization_invalid", "device authorization not_before is invalid", {}
    if now + timedelta(minutes=5) < not_before:
        return False, "device_authorization_invalid", "device authorization is not valid yet", {}
    if expires_at is not None and now > expires_at:
        return False, "device_authorization_invalid", "device authorization is expired", {}
    return True, "", "", payload_data


def _psql_json(sql, variables):
    cfg = _load_db_env()
    env = os.environ.copy()
    env['PGPASSWORD'] = cfg.get('AX8_AUTH_DB_PASSWORD', '')
    cmd = [
        '/usr/bin/psql',
        '-h', cfg.get('AX8_AUTH_DB_HOST', '127.0.0.1'),
        '-p', cfg.get('AX8_AUTH_DB_PORT', '5432'),
        '-U', cfg.get('AX8_AUTH_DB_USER', '8ax_auth_app'),
        '-d', cfg.get('AX8_AUTH_DB_NAME', '8ax_auth'),
        '-X', '-q', '-t', '-A', '--no-psqlrc',
    ]
    for key, value in variables.items():
        cmd += ['-v', f'{key}={value if value is not None else ""}']
    return subprocess.run(cmd, env=env, text=True, input=sql, capture_output=True, timeout=15)


def _saved_device_id_for_pl_dna(pl_dna, pl_dna_hash):
    sql = """
SELECT COALESCE((
  SELECT json_build_object('deviceId', device_id)::text
  FROM devices
  WHERE pl_device_dna = :'pl_device_dna'
     OR lower(COALESCE(pl_dna_hash, '')) = lower(:'pl_dna_hash')
  ORDER BY COALESCE(factory_registered_at, updated_at, created_at) DESC, device_id
  LIMIT 1
), '{}')::text;
"""
    result = _psql_json(sql, {"pl_device_dna": pl_dna, "pl_dna_hash": pl_dna_hash})
    if result.returncode != 0:
        return None
    lines = (result.stdout or "").strip().splitlines()
    if not lines:
        return ""
    try:
        data = json.loads(lines[-1])
    except Exception:
        return None
    return str(data.get("deviceId") or "").strip()


def _device_id_owner_hash(device_id):
    sql = """
SELECT COALESCE((
  SELECT json_build_object('plDnaHash', COALESCE(pl_dna_hash, ''))::text
  FROM devices
  WHERE device_id = :'device_id'
  LIMIT 1
), '{}')::text;
"""
    result = _psql_json(sql, {"device_id": device_id})
    if result.returncode != 0:
        return None
    lines = (result.stdout or "").strip().splitlines()
    if not lines:
        return ""
    try:
        data = json.loads(lines[-1])
    except Exception:
        return None
    return str(data.get("plDnaHash") or "").strip().lower()


def _allocate_pl_dna_device_id(pl_dna, pl_dna_hash):
    saved = _saved_device_id_for_pl_dna(pl_dna, pl_dna_hash)
    if saved is None:
        return "", "lookup_failed"
    if saved:
        return saved, "saved"
    for attempt in range(1000000):
        candidate = _pl_dna_short_device_id(pl_dna, attempt)
        owner_hash = _device_id_owner_hash(candidate)
        if owner_hash is None:
            return "", "lookup_failed"
        if not owner_hash or owner_hash == pl_dna_hash:
            return candidate, "allocated" if attempt == 0 else f"allocated_retry_{attempt}"
    return "", "exhausted"


def _valid_username(value):
    return bool(re.fullmatch(r'[A-Za-z0-9_@.\-]{2,64}', value or ''))

def _safe_text(value):
    return html.escape(str(value or ""), quote=True)

def _js_string(value):
    return json.dumps(str(value or ""), ensure_ascii=False)

def payload(status="ok", extra=None):
    data = {
        "status": status,
        "service": SERVICE,
        "version": VERSION,
        "host": socket.gethostname(),
        "started_at": STARTED_AT,
        "time": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    }
    if extra:
        data.update(extra)
    return data


class Handler(BaseHTTPRequestHandler):
    server_version = "8ax-auth-gateway/" + VERSION

    def _client_ip(self):
        for header in ("CF-Connecting-IP", "X-Real-IP", "X-Forwarded-For"):
            value = str(self.headers.get(header, "") or "").strip()
            if not value:
                continue
            if header == "X-Forwarded-For":
                value = value.split(",", 1)[0].strip()
            if value:
                return value[:64]
        return (self.client_address[0] if self.client_address else "")[:64]

    def _json(self, code, data):
        body = json.dumps(data, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _html(self, code, body):
        raw = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def _redirect(self, location):
        self.send_response(303)
        self.send_header("Location", location)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", "0")
        self.end_headers()

    def _admin_auth_ok(self):
        if not self._admin_origin_ok():
            self._json(404, payload("not_found", {"path": urlparse(self.path).path}))
            return False
        cfg = _load_admin_env()
        expected_user = cfg.get("AX8_ADMIN_USER")
        expected_password = cfg.get("AX8_ADMIN_PASSWORD")
        if not expected_user or not expected_password:
            self._json(503, payload("admin_auth_not_configured", {"message": "厂家审核后台账号密码尚未配置。"}))
            return False
        header = self.headers.get("Authorization", "")
        if not header.startswith("Basic "):
            self.send_response(401)
            self.send_header("WWW-Authenticate", 'Basic realm="6x-cnc admin review"')
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return False
        try:
            decoded = base64.b64decode(header.removeprefix("Basic ").strip()).decode("utf-8")
            user, password = decoded.split(":", 1)
        except Exception:
            self._json(401, payload("bad_admin_auth"))
            return False
        if user != expected_user or password != expected_password:
            self._json(403, payload("admin_auth_denied"))
            return False
        return True

    def _admin_origin_ok(self):
        path = urlparse(self.path).path
        host = (self.headers.get("Host") or "").split(":", 1)[0].lower()
        if host in ("127.0.0.1", "localhost", "::1"):
            return True
        if path.startswith("/api/v1/admin/") and host in ("license.cjwsjzyy.xyz", "license.3dtouch.top"):
            return True
        if self.headers.get("CF-Connecting-IP") or self.headers.get("Cf-Ray"):
            return False
        return host in ("127.0.0.1", "localhost")

    def _admin_dealers_page(self):
        sql = """
SELECT COALESCE(json_agg(row_to_json(t)), '[]'::json)::text
FROM (
  SELECT dealer_id::text, username, dealer_name, review_status, cooperation_status,
         contact_name, phone, wechat, customer_contact_name, customer_phone, customer_wechat,
         review_note, created_at
  FROM dealers
  ORDER BY CASE WHEN review_status = 'pending_review' THEN 0 ELSE 1 END, created_at DESC
  LIMIT 100
) t;
"""
        result = _psql_json(sql, {})
        if result.returncode != 0:
            self._html(500, "<h1>读取经销商列表失败</h1><pre>%s</pre>" % _safe_text(result.stderr))
            return
        lines = (result.stdout or "").strip().splitlines()
        dealers = json.loads(lines[-1]) if lines else []
        rows = []
        for item in dealers:
            dealer_id = _safe_text(item.get("dealer_id"))
            status = _safe_text(item.get("review_status"))
            can_review = item.get("review_status") == "pending_review"
            actions = "-"
            if can_review:
                actions = f"""
                <form method="post" action="/api/v1/admin/dealers/review" class="actions">
                  <input type="hidden" name="dealerId" value="{dealer_id}">
                  <input type="text" name="note" placeholder="审核备注，可空">
                  <button name="decision" value="approved">通过</button>
                  <button name="decision" value="rejected" class="danger">拒绝</button>
                </form>
                """
            rows.append(f"""
              <tr>
                <td>{_safe_text(item.get("created_at"))}</td>
                <td><code>{dealer_id}</code></td>
                <td>{_safe_text(item.get("username"))}</td>
                <td>{status}</td>
                <td>{_safe_text(item.get("cooperation_status"))}</td>
                <td>
                  <strong>厂家联系</strong><br>
                  {_safe_text(item.get("contact_name"))} / {_safe_text(item.get("phone"))} / {_safe_text(item.get("wechat"))}<br>
                  <strong>终端用户联系</strong><br>
                  {_safe_text(item.get("customer_contact_name"))} / {_safe_text(item.get("customer_phone"))} / {_safe_text(item.get("customer_wechat"))}
                </td>
                <td>{_safe_text(item.get("review_note"))}</td>
                <td>{actions}</td>
              </tr>
            """)
        body = f"""<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <title>6x-cnc 经销商审核</title>
  <style>
    body {{ font-family: "Microsoft YaHei UI", Arial, sans-serif; margin: 24px; color: #111; }}
    h1 {{ font-size: 24px; margin: 0 0 16px; }}
    .hint {{ margin-bottom: 16px; color: #555; }}
    table {{ border-collapse: collapse; width: 100%; font-size: 14px; }}
    th, td {{ border: 1px solid #ddd; padding: 8px; vertical-align: top; }}
    th {{ background: #f3f4f6; text-align: left; }}
    code {{ font-size: 12px; }}
    input {{ height: 28px; min-width: 180px; }}
    button {{ height: 32px; margin-left: 6px; padding: 0 16px; }}
    .danger {{ color: #b00020; }}
    .actions {{ white-space: nowrap; }}
  </style>
</head>
<body>
  <h1>6x-cnc 经销商审核</h1>
  <div class="hint">待审核账号通过后才能登录经销商客户端。拒绝后账号保留记录但不能登录。</div>
  <table>
    <thead>
      <tr><th>提交时间</th><th>经销商ID</th><th>用户名</th><th>审核</th><th>合作</th><th>联系方式</th><th>备注</th><th>操作</th></tr>
    </thead>
    <tbody>{''.join(rows) if rows else '<tr><td colspan="8">暂无经销商记录</td></tr>'}</tbody>
  </table>
</body>
</html>"""
        self._html(200, body)

    def _handle_admin_dealers_json(self):
        sql = """
SELECT json_build_object(
  'success', true,
  'dealers', COALESCE(json_agg(row_to_json(t)), '[]'::json),
  'message', 'ok'
)::text
FROM (
  SELECT dealer_id::text AS "dealerId",
         lpad(public_no::text, 4, '0') AS "dealerNo",
         username AS "username",
         dealer_name AS "dealerName",
         review_status AS "reviewStatus",
         cooperation_status AS "cooperationStatus",
         contact_name AS "contactName",
         phone AS "phone",
         wechat AS "wechat",
         customer_contact_name AS "customerContactName",
         customer_phone AS "customerPhone",
         customer_wechat AS "customerWechat",
         review_note AS "reviewNote",
         created_at AS "createdAt"
  FROM dealers
  ORDER BY CASE WHEN review_status = 'pending_review' THEN 0 ELSE 1 END, created_at DESC
  LIMIT 200
) t;
"""
        result = _psql_json(sql, {})
        if result.returncode != 0:
            self._json(500, payload("admin_dealers_failed", {"message": "读取经销商列表失败。"}))
            return
        lines = (result.stdout or "").strip().splitlines()
        self._json(200, json.loads(lines[-1]) if lines else {"success": False, "dealers": [], "message": "empty"})

    def _handle_admin_dealer_users_json(self):
        query = parse_qs(urlparse(self.path).query)
        dealer_id = (query.get("dealerId", [""])[0] or "").strip()
        sql = """
SELECT json_build_object(
  'success', true,
  'users', COALESCE(json_agg(row_to_json(t)), '[]'::json),
  'message', 'ok'
)::text
FROM (
  SELECT dealer_user_id::text AS "dealerUserId",
         lpad(public_no::text, 4, '0') AS "dealerUserNo",
         dealer_id::text AS "dealerId",
         username AS "username",
         display_name AS "displayName",
         phone AS "phone",
         role AS "role",
         status AS "status",
         can_handle_requests AS "canHandleRequests",
         can_show_daily_code AS "canShowDailyCode",
         last_login_at AS "lastLoginAt",
         created_at AS "createdAt"
  FROM dealer_users
  WHERE (NULLIF(:'dealer_id', '') IS NULL OR dealer_id = :'dealer_id'::uuid)
  ORDER BY created_at DESC
  LIMIT 200
) t;
"""
        result = _psql_json(sql, {"dealer_id": dealer_id})
        if result.returncode != 0:
            self._json(500, payload("admin_dealer_users_failed", {"message": "读取员工账号失败。"}))
            return
        lines = (result.stdout or "").strip().splitlines()
        self._json(200, json.loads(lines[-1]) if lines else {"success": False, "users": [], "message": "empty"})

    def _handle_admin_upgrade_requests_json(self):
        sql = """
SELECT json_build_object(
  'success', true,
  'requests', COALESCE(json_agg(row_to_json(t)), '[]'::json),
  'message', 'ok'
)::text
FROM (
  SELECT r.upgrade_request_id::text AS "upgradeRequestId",
         lpad(r.public_no::text, 4, '0') AS "upgradeRequestNo",
         r.device_id AS "deviceId",
         r.dealer_id::text AS "dealerId",
         d.username AS "dealerUsername",
         d.dealer_name AS "dealerName",
         r.dealer_user_id::text AS "dealerUserId",
         u.username AS "employeeUsername",
         u.display_name AS "employeeName",
         r.status AS "status",
         r.request_payload AS "requestPayload",
         r.qr_digest AS "qrDigest",
         r.created_at AS "createdAt",
         r.expires_at AS "expiresAt"
  FROM upgrade_requests r
  LEFT JOIN dealers d ON d.dealer_id = r.dealer_id
  LEFT JOIN dealer_users u ON u.dealer_user_id = r.dealer_user_id
  ORDER BY r.created_at DESC
  LIMIT 200
) t;
"""
        result = _psql_json(sql, {})
        if result.returncode != 0:
            self._json(500, payload("admin_upgrade_requests_failed", {"message": "读取终端升级请求失败。"}))
            return
        lines = (result.stdout or "").strip().splitlines()
        self._json(200, json.loads(lines[-1]) if lines else {"success": False, "requests": [], "message": "empty"})

    def _handle_admin_devices_json(self):
        if not _ensure_device_security_schema():
            self._json(500, payload("device_security_schema_unavailable", {"message": "device security schema is unavailable"}))
            return
        sql = """
SELECT json_build_object(
  'success', true,
  'devices', COALESCE(json_agg(row_to_json(t)), '[]'::json),
  'message', 'ok'
)::text
FROM (
  SELECT d.device_id AS "deviceId",
         CASE WHEN d.device_id ~ '^[0-9]{6}$' THEN d.device_id ELSE '' END AS "vpsDistributionId",
         d.pl_device_dna AS "plDeviceDna",
         d.pl_dna_hash AS "plDnaHash",
         d.device_id_source AS "deviceIdSource",
         d.activation_status AS "activationStatus",
         d.initial_version AS "initialVersion",
         d.current_version AS "currentVersion",
         d.device_public_key_sha256 AS "devicePublicKeySha256",
         d.factory_registered_at AS "factoryRegisteredAt",
         d.created_at AS "createdAt",
         d.updated_at AS "updatedAt",
         COALESCE(ip.ip_access_records, '[]'::json) AS "ipAccessRecords",
         COALESCE(ip.latest_ip_access, '') AS "latestIpAccess"
  FROM devices d
  LEFT JOIN LATERAL (
    SELECT json_agg(
             json_build_object(
               'nonce', x.nonce,
               'time', x.access_time,
               'ip', x.ip_address,
               'status', x.access_status,
               'path', x.access_path
             )
             ORDER BY x.access_time DESC
           ) AS ip_access_records,
           (array_agg(
              to_char(x.access_time AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD HH24:MI')
              || ' '
              || COALESCE(x.ip_address, '-')
              || ' '
              || x.access_status
              ORDER BY x.access_time DESC
            ))[1] AS latest_ip_access
    FROM (
      SELECT COALESCE(c.used_at, c.created_at) AS access_time,
             c.nonce AS nonce,
             host(COALESCE(c.used_request_ip, c.request_ip)) AS ip_address,
             CASE WHEN c.used_at IS NULL THEN 'challenge' ELSE 'download' END AS access_status,
             COALESCE(c.used_path, '') AS access_path
      FROM device_challenges c
      WHERE c.device_id = d.device_id
        AND c.purpose = 'drive_profile_download'
      ORDER BY COALESCE(c.used_at, c.created_at) DESC
      LIMIT 20
    ) x
  ) ip ON true
  WHERE d.pl_device_dna IS NOT NULL OR d.pl_dna_hash IS NOT NULL OR d.device_id_source IS NOT NULL
  ORDER BY COALESCE(d.factory_registered_at, d.created_at) DESC, d.device_id
  LIMIT 500
) t;
"""
        result = _psql_json(sql, {})
        if result.returncode != 0:
            self._json(500, payload("admin_devices_failed", {"message": "读取设备 DNA 登记列表失败。"}))
            return
        lines = (result.stdout or "").strip().splitlines()
        response = json.loads(lines[-1]) if lines else {"success": False, "devices": [], "message": "empty"}
        for device in response.get("devices", []):
            target = self._device_authorization_path(device.get("deviceId"), device.get("plDnaHash"))
            device["authorizationStatus"] = "authorized" if target is not None and target.exists() else "pending_factory_authorization"
        self._json(200, response)


    def _device_private_dir(self, device_id, pl_dna_hash=None):
        device_id = str(device_id or "").strip()
        if not re.fullmatch(r"\d{6}", device_id):
            return None
        pl_dna_hash = str(pl_dna_hash or "").strip().lower()
        if not re.fullmatch(r"[0-9a-f]{64}", pl_dna_hash):
            return None
        return DEVICE_PRIVATE_ROOT / f"{device_id}-{pl_dna_hash}"

    def _device_authorization_path(self, device_id, pl_dna_hash):
        private_dir = self._device_private_dir(device_id, pl_dna_hash)
        return None if private_dir is None else private_dir / "device_authorization.json"

    def _ensure_device_private_layout(self, device_id, pl_dna_hash):
        private_dir = self._device_private_dir(device_id, pl_dna_hash)
        if private_dir is None:
            return None
        private_dir.mkdir(parents=True, exist_ok=True)
        (private_dir / "ota").mkdir(parents=True, exist_ok=True)
        (private_dir / "winremote-uploads").mkdir(parents=True, exist_ok=True)
        return private_dir

    def _device_record_by_hash(self, pl_dna_hash):
        if not _ensure_device_security_schema() or not pl_dna_hash:
            return None
        sql = """
SELECT COALESCE((
  SELECT json_build_object(
    'device_id', device_id,
    'activation_status', activation_status,
    'pl_dna_hash', pl_dna_hash,
    'device_public_key_pem', device_public_key_pem,
    'device_public_key_sha256', device_public_key_sha256,
    'device_auth_revoked_at', device_auth_revoked_at,
    'device_auth_revocation_reason', device_auth_revocation_reason
  )::text
  FROM devices
  WHERE lower(pl_dna_hash) = lower(:'pl_dna_hash')
  LIMIT 1
), '') AS payload;
"""
        result = _psql_json(sql, {"pl_dna_hash": pl_dna_hash})
        if result.returncode != 0:
            return None
        line = (result.stdout or "").strip().splitlines()[-1:] or [""]
        if not line[0]:
            return None
        try:
            record = json.loads(line[0])
        except Exception:
            return None
        return record if isinstance(record, dict) else None

    def _handle_admin_device_authorization_upload(self, body):
        try:
            data = json.loads(body.decode("utf-8") or "{}")
        except Exception:
            self._json(400, {"success": False, "message": "请求内容不是有效 JSON。"})
            return
        device_id = str(data.get("deviceId") or data.get("device_id") or "").strip()
        pl_dna_hash = str(data.get("plDnaHash") or data.get("pl_dna_hash") or "").strip().lower()
        envelope = data.get("deviceAuthorization") or data.get("device_authorization")
        if not device_id or not pl_dna_hash or not isinstance(envelope, dict):
            self._json(400, payload("device_authorization_upload_bad_request", {"message": "缺少 deviceId、plDnaHash 或 deviceAuthorization。"}))
            return
        if not re.fullmatch(r"[0-9a-f]{64}", pl_dna_hash):
            self._json(400, payload("device_authorization_upload_bad_hash", {"message": "plDnaHash 格式无效。"}))
            return
        payload_data = envelope.get("payload") if isinstance(envelope, dict) else None
        signature = envelope.get("signature") if isinstance(envelope, dict) else None
        if envelope.get("schema") != DEVICE_AUTH_ENVELOPE_SCHEMA or not isinstance(payload_data, dict) or not isinstance(signature, dict):
            self._json(400, payload("device_authorization_upload_bad_envelope", {"message": "授权信封结构无效。"}))
            return
        if str(payload_data.get("schema") or "") != DEVICE_AUTH_SCHEMA:
            self._json(400, payload("device_authorization_upload_bad_schema", {"message": "授权 payload schema 无效。"}))
            return
        if str(payload_data.get("device_id") or "") != device_id:
            self._json(400, payload("device_authorization_upload_device_mismatch", {"message": "授权 device_id 与上传目标不一致。"}))
            return
        if str(payload_data.get("pl_device_dna_hash") or "").lower() != pl_dna_hash:
            self._json(400, payload("device_authorization_upload_dna_mismatch", {"message": "授权 DNA 摘要与上传目标不一致。"}))
            return
        record = self._device_record_by_hash(pl_dna_hash)
        if not record or str(record.get("device_id") or "") != device_id:
            self._json(404, payload("device_authorization_upload_device_missing", {"message": "未找到对应设备 DNA 登记。"}))
            return
        expected_pub = str(record.get("device_public_key_sha256") or "").strip().lower()
        actual_pub = str(payload_data.get("device_public_key_sha256") or "").strip().lower()
        if expected_pub and actual_pub != expected_pub:
            self._json(400, payload("device_authorization_upload_public_key_mismatch", {"message": "授权设备公钥指纹与登记记录不一致。"}))
            return
        target = self._device_authorization_path(device_id, pl_dna_hash)
        if target is None:
            self._json(400, payload("device_authorization_upload_bad_device_id", {"message": "deviceId 必须是 VPS 保存的 6 位分发 ID。"}))
            return
        target.parent.mkdir(parents=True, exist_ok=True)
        tmp = target.with_name(target.name + ".tmp")
        raw = json.dumps(envelope, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
        tmp.write_bytes(raw)
        tmp.replace(target)
        try:
            target.chmod(0o640)
        except Exception:
            pass
        signature_hash = hashlib.sha256(str(signature.get("value") or "").encode("ascii", errors="ignore")).hexdigest()
        self._json(200, {
            "success": True,
            "message": "设备授权文件已保存，板端可下载。",
            "deviceId": device_id,
            "plDnaHash": pl_dna_hash,
            "signatureHash": signature_hash,
        })

    def _handle_device_authorization_download(self):
        dna = self._device_dna_from_request()
        if not dna:
            self._json(401, payload("device_dna_missing", {"message": "authorization download requires live device DNA"}))
            return
        record = self._device_record_by_dna(dna)
        if not record:
            self._json(403, payload("device_not_factory_registered", {"message": "授权下载前必须先登记本机 DNA。"}))
            return
        pl_dna_hash = str(record.get("pl_dna_hash") or _pl_dna_hash(dna)).strip().lower()
        device_id = str(record.get("device_id") or "").strip()
        target = self._device_authorization_path(device_id, pl_dna_hash)
        if target is None:
            self._json(500, payload("device_private_id_invalid", {"message": "VPS 设备登记缺少有效 6 位分发 ID。"}))
            return
        if not target.exists():
            self._json(404, payload("device_authorization_missing", {"message": "工厂授权文件尚未上传。"}))
            return
        try:
            envelope = json.loads(target.read_text(encoding="utf-8"))
        except Exception:
            self._json(500, payload("device_authorization_corrupt", {"message": "服务器授权文件损坏。"}))
            return
        self._json(200, {
            "success": True,
            "message": "ok",
            "deviceId": device_id,
            "plDnaHash": pl_dna_hash,
            "deviceAuthorization": envelope,
        })


    def _handle_upgrade_request_review(self, body):
        try:
            data = json.loads(body.decode("utf-8") or "{}")
        except Exception:
            self._json(400, {"message": "请求内容不是有效 JSON。"})
            return
        request_id = (data.get("upgradeRequestId") or data.get("request_id") or "").strip()
        decision = (data.get("decision") or "").strip()
        note = (data.get("note") or "").strip()
        if decision not in ("approved", "rejected", "verified"):
            self._json(400, {"message": "decision 只能是 approved、rejected 或 verified。"})
            return
        sql = """
WITH upd AS (
  UPDATE upgrade_requests
  SET status = :'decision'
  WHERE upgrade_request_id = :'request_id'::uuid
  RETURNING upgrade_request_id::text, device_id, status
), audit AS (
  INSERT INTO audit_events(actor_type, actor_id, event_type, entity_type, entity_id, ip_address, detail)
  SELECT 'admin', :'reviewed_by', 'upgrade_request.review.' || status, 'upgrade_request', upgrade_request_id, NULLIF(:'ip_address', '')::inet,
         jsonb_build_object('decision', status, 'note', :'note')
  FROM upd
)
SELECT COALESCE((SELECT json_build_object('success', true, 'upgradeRequestId', upgrade_request_id, 'deviceId', device_id, 'status', status, 'message', '终端升级请求状态已更新。')::text FROM upd),
                json_build_object('success', false, 'message', 'upgrade request not found')::text);
"""
        result = _psql_json(sql, {
            "request_id": request_id,
            "decision": decision,
            "note": note,
            "reviewed_by": _load_admin_env().get("AX8_ADMIN_USER", "admin"),
            "ip_address": self.client_address[0] if self.client_address else "",
        })
        if result.returncode != 0:
            self._json(500, {"message": "终端升级请求审批保存失败。"})
            return
        lines = (result.stdout or "").strip().splitlines()
        response = json.loads(lines[-1]) if lines else {"success": False, "message": "审批失败。"}
        self._json(200 if response.get("success") else 400, response)

    def _handle_admin_delete(self, body):
        try:
            data = json.loads(body.decode("utf-8") or "{}")
        except Exception:
            self._json(400, {"message": "请求内容不是有效 JSON。"})
            return
        target_type = (data.get("targetType") or "").strip()
        target_id = (data.get("targetId") or "").strip()
        note = (data.get("note") or "").strip()
        if target_type not in ("dealer", "dealer_user", "upgrade_request", "device", "device_ip_access"):
            self._json(400, {"message": "targetType 只能是 dealer、dealer_user、upgrade_request、device 或 device_ip_access。"})
            return
        if not target_id:
            self._json(400, {"message": "缺少 targetId。"})
            return
        device_delete_pl_dna_hash = ""
        if target_type == "device":
            hash_result = _psql_json(
                "SELECT COALESCE((SELECT pl_dna_hash FROM devices WHERE device_id = :'target_id' LIMIT 1), '') AS pl_dna_hash;",
                {"target_id": target_id},
            )
            if hash_result.returncode == 0:
                hash_lines = (hash_result.stdout or "").strip().splitlines()
                device_delete_pl_dna_hash = hash_lines[-1].strip().lower() if hash_lines else ""
        if target_type == "dealer":
            sql = """
WITH upd AS (
  UPDATE dealers
  SET review_status = CASE WHEN review_status = 'pending_review' THEN 'rejected' ELSE 'suspended' END,
      cooperation_status = 'disabled',
      allow_contact_display = false,
      review_note = NULLIF(:'note', ''),
      updated_at = now()
  WHERE dealer_id = :'target_id'::uuid
  RETURNING dealer_id::text AS id, username AS name
), audit AS (
  INSERT INTO audit_events(actor_type, actor_id, event_type, entity_type, entity_id, ip_address, detail)
  SELECT 'admin', :'operator', 'dealer.disabled', 'dealer', id, NULLIF(:'ip_address', '')::inet, jsonb_build_object('note', :'note')
  FROM upd
)
SELECT COALESCE((SELECT json_build_object('success', true, 'message', '经销商已禁用。', 'targetId', id, 'name', name)::text FROM upd),
                json_build_object('success', false, 'message', 'dealer not found')::text);
"""
        elif target_type == "dealer_user":
            sql = """
WITH upd AS (
  UPDATE dealer_users
  SET status = 'disabled',
      can_handle_requests = false,
      can_show_daily_code = false,
      updated_at = now()
  WHERE dealer_user_id = :'target_id'::uuid
  RETURNING dealer_user_id::text AS id, username AS name
), audit AS (
  INSERT INTO audit_events(actor_type, actor_id, event_type, entity_type, entity_id, ip_address, detail)
  SELECT 'admin', :'operator', 'dealer_user.disabled', 'dealer_user', id, NULLIF(:'ip_address', '')::inet, jsonb_build_object('note', :'note')
  FROM upd
)
SELECT COALESCE((SELECT json_build_object('success', true, 'message', '员工账号已禁用。', 'targetId', id, 'name', name)::text FROM upd),
                json_build_object('success', false, 'message', 'dealer user not found')::text);
"""
        elif target_type == "device":
            sql = """
WITH candidate AS (
  SELECT device_id, pl_device_dna, pl_dna_hash, activation_status
  FROM devices
  WHERE device_id = :'target_id'
), del AS (
  DELETE FROM devices d
  WHERE d.device_id = :'target_id'
    AND d.activation_status = 'factory_registered'
    AND NOT EXISTS (SELECT 1 FROM upgrade_requests r WHERE r.device_id = d.device_id)
  RETURNING d.device_id AS id, COALESCE(d.pl_device_dna, d.pl_dna_hash, '') AS name, d.pl_dna_hash
), audit AS (
  INSERT INTO audit_events(actor_type, actor_id, event_type, entity_type, entity_id, ip_address, detail)
  SELECT 'admin', :'operator', 'device.deleted', 'device', id, NULLIF(:'ip_address', '')::inet,
         jsonb_build_object('note', :'note', 'plDnaHash', pl_dna_hash)
  FROM del
)
SELECT CASE
  WHEN EXISTS (SELECT 1 FROM del) THEN
    (SELECT json_build_object('success', true, 'message', '设备 DNA 登记已删除。', 'targetId', id, 'name', name)::text FROM del)
  WHEN NOT EXISTS (SELECT 1 FROM candidate) THEN
    json_build_object('success', false, 'message', 'device not found')::text
  ELSE
    (SELECT json_build_object(
      'success', false,
      'message', CASE
        WHEN activation_status <> 'factory_registered' THEN '设备已出库或已激活，不允许删除 DNA 登记。'
        ELSE '设备已有升级请求或业务引用，不允许删除。'
      END,
      'targetId', device_id
    )::text FROM candidate)
END;
"""
        elif target_type == "upgrade_request":
            sql = """
WITH del AS (
  DELETE FROM upgrade_requests
  WHERE upgrade_request_id = :'target_id'::uuid
  RETURNING upgrade_request_id::text AS id, device_id AS name
), audit AS (
  INSERT INTO audit_events(actor_type, actor_id, event_type, entity_type, entity_id, ip_address, detail)
  SELECT 'admin', :'operator', 'upgrade_request.deleted', 'upgrade_request', id, NULLIF(:'ip_address', '')::inet, jsonb_build_object('note', :'note', 'deviceId', name)
  FROM del
)
SELECT COALESCE((SELECT json_build_object('success', true, 'message', '终端升级请求已删除。', 'targetId', id, 'name', name)::text FROM del),
                json_build_object('success', false, 'message', 'upgrade request not found')::text);
"""
        else:
            if not _ensure_device_security_schema():
                self._json(500, {"message": "device security schema is unavailable"})
                return
            sql = """
WITH del AS (
  DELETE FROM device_challenges
  WHERE nonce = :'target_id'
  RETURNING nonce AS id,
            device_id,
            host(COALESCE(used_request_ip, request_ip)) AS access_ip,
            COALESCE(used_at, created_at) AS access_time,
            COALESCE(used_path, '') AS access_path
), audit AS (
  INSERT INTO audit_events(actor_type, actor_id, event_type, entity_type, entity_id, ip_address, detail)
  SELECT 'admin', :'operator', 'device_ip_access.deleted', 'device_ip_access', id, NULLIF(:'ip_address', '')::inet,
         jsonb_build_object(
           'note', :'note',
           'deviceId', device_id,
           'accessIp', access_ip,
           'accessPath', access_path,
           'accessTime', access_time
         )
  FROM del
)
SELECT COALESCE((SELECT json_build_object('success', true, 'message', 'IP 访问记录已删除。', 'targetId', id, 'name', device_id || ' ' || COALESCE(access_ip, '-'))::text FROM del),
                json_build_object('success', false, 'message', 'device ip access record not found')::text);
"""
        result = _psql_json(sql, {
            "target_id": target_id,
            "note": note,
            "operator": _load_admin_env().get("AX8_ADMIN_USER", "admin"),
            "ip_address": self._client_ip(),
        })
        if result.returncode != 0:
            self._json(500, {"message": "删除/禁用操作失败。"})
            return
        lines = (result.stdout or "").strip().splitlines()
        response = json.loads(lines[-1]) if lines else {"success": False, "message": "操作失败。"}
        if target_type == "device" and response.get("success"):
            authorization_deleted = False
            auth_path = self._device_authorization_path(response.get("targetId"), device_delete_pl_dna_hash)
            if auth_path is not None and auth_path.exists():
                try:
                    auth_path.unlink()
                    authorization_deleted = True
                except Exception as exc:
                    self._json(500, {
                        "success": False,
                        "message": f"设备 DNA 登记已删除，但授权文件删除失败：{exc}",
                        "targetId": response.get("targetId"),
                        "authorizationDeleted": False,
                    })
                    return
            response["authorizationDeleted"] = authorization_deleted
            response["message"] = str(response.get("message") or "")
            response["message"] += " 已同步删除授权文件。" if authorization_deleted else " 未发现授权文件。"
            try:
                remote_ssh_gateway.sync_authorized_keys(_psql_json)
            except remote_ssh_gateway.RemoteSshError as exc:
                self._json(500, {
                    "success": False,
                    "message": "设备登记已删除，但远程 SSH key 清理失败：%s" % exc,
                    "targetId": response.get("targetId"),
                    "authorizationDeleted": authorization_deleted,
                })
                return
        self._json(200 if response.get("success") else 400, response)

    def _handle_dealer_review(self, body):
        content_type = self.headers.get("Content-Type", "")
        if "application/json" in content_type:
            try:
                data = json.loads(body.decode("utf-8") or "{}")
            except Exception:
                self._json(400, payload("bad_json"))
                return
        else:
            parsed = parse_qs(body.decode("utf-8") if body else "")
            data = {key: values[-1] for key, values in parsed.items()}
        dealer_id = (data.get("dealerId") or data.get("dealer_id") or "").strip()
        decision = (data.get("decision") or "").strip()
        note = (data.get("note") or "").strip()
        if decision not in ("approved", "rejected"):
            self._json(400, payload("bad_decision", {"message": "decision 只能是 approved 或 rejected。"}))
            return
        sql = """
WITH upd AS (
  UPDATE dealers
  SET review_status = :'decision',
      cooperation_status = CASE WHEN :'decision' = 'approved' THEN 'active' ELSE 'disabled' END,
      allow_contact_display = CASE WHEN :'decision' = 'approved' THEN true ELSE false END,
      reviewed_by = :'reviewed_by',
      reviewed_at = now(),
      review_note = NULLIF(:'note', ''),
      updated_at = now()
  WHERE dealer_id = :'dealer_id'::uuid
  RETURNING dealer_id::text, username, review_status, cooperation_status
), audit AS (
  INSERT INTO audit_events(actor_type, actor_id, event_type, entity_type, entity_id, ip_address, detail)
  SELECT 'admin', :'reviewed_by', 'dealer.review.' || review_status, 'dealer', dealer_id, NULLIF(:'ip_address', '')::inet,
         jsonb_build_object('decision', review_status, 'note', :'note')
  FROM upd
)
SELECT COALESCE((SELECT json_build_object('success', true, 'dealerId', dealer_id, 'username', username, 'reviewStatus', review_status, 'cooperationStatus', cooperation_status)::text FROM upd),
                json_build_object('success', false, 'error', 'dealer not found')::text);
"""
        result = _psql_json(sql, {
            "dealer_id": dealer_id,
            "decision": decision,
            "note": note,
            "reviewed_by": _load_admin_env().get("AX8_ADMIN_USER", "admin"),
            "ip_address": self.client_address[0] if self.client_address else "",
        })
        if result.returncode != 0:
            self._json(500, payload("dealer_review_failed", {"message": "审核保存失败。"}))
            return
        if "application/json" in content_type:
            lines = (result.stdout or "").strip().splitlines()
            self._json(200, json.loads(lines[-1]) if lines else {"success": False})
        else:
            self._redirect("/admin/dealers")

    def _dealer_from_bearer(self):
        header = self.headers.get("Authorization", "")
        if not header.startswith("Bearer "):
            return None
        token = header.removeprefix("Bearer ").strip()
        if not token:
            return None
        token_hash = hashlib.sha256(token.encode("utf-8")).hexdigest()
        sql = """
SELECT COALESCE((SELECT json_build_object('dealerId', d.dealer_id::text, 'username', d.username, 'dealerName', d.dealer_name)::text
FROM app_sessions s
JOIN dealers d ON d.dealer_id = s.subject_id
WHERE s.subject_type = 'dealer'
  AND s.token_hash = :'token_hash'
  AND s.revoked_at IS NULL
  AND s.expires_at > now()
  AND d.review_status = 'approved'
  AND d.cooperation_status = 'active'
LIMIT 1), '{}'::json::text);
"""
        result = _psql_json(sql, {"token_hash": token_hash})
        if result.returncode != 0:
            return None
        lines = (result.stdout or "").strip().splitlines()
        if not lines:
            return None
        data = json.loads(lines[-1])
        return data if data.get("dealerId") else None

    def _handle_dealer_login(self, body):
        try:
            data = json.loads(body.decode("utf-8") or "{}")
        except Exception:
            self._json(400, {"message": "请求内容不是有效 JSON。"})
            return
        username = (data.get("username") or "").strip()
        password = data.get("password") or ""
        token = secrets.token_urlsafe(32)
        token_hash = hashlib.sha256(token.encode("utf-8")).hexdigest()
        expires_at = (datetime.now(timezone.utc) + timedelta(days=7)).isoformat()
        sql = """
WITH dealer_auth AS (
  SELECT dealer_id, username, dealer_name, review_status, cooperation_status
  FROM dealers
  WHERE username = :'username'
    AND password_hash = crypt(:'password', password_hash)
  LIMIT 1
), active_dealer AS (
  SELECT * FROM dealer_auth
  WHERE review_status = 'approved' AND cooperation_status = 'active'
), staff_auth AS (
  SELECT u.dealer_user_id, u.dealer_id, u.username, u.display_name, u.role, u.can_handle_requests, u.can_show_daily_code,
         d.dealer_name, d.review_status, d.cooperation_status
  FROM dealer_users u
  JOIN dealers d ON d.dealer_id = u.dealer_id
  WHERE u.username = :'username'
    AND u.password_hash = crypt(:'password', u.password_hash)
    AND u.status = 'active'
  LIMIT 1
), active_staff AS (
  SELECT * FROM staff_auth
  WHERE review_status = 'approved' AND cooperation_status = 'active'
), dealer_sess AS (
  INSERT INTO app_sessions(subject_type, subject_id, token_hash, machine_digest, ip_address, user_agent, expires_at)
  SELECT 'dealer', dealer_id, :'token_hash', NULLIF(:'machine_digest', ''), NULLIF(:'ip_address', '')::inet, :'user_agent', :'expires_at'::timestamptz
  FROM active_dealer
  RETURNING expires_at
), staff_sess AS (
  INSERT INTO app_sessions(subject_type, subject_id, token_hash, machine_digest, ip_address, user_agent, expires_at)
  SELECT 'dealer_user', dealer_user_id, :'token_hash', NULLIF(:'machine_digest', ''), NULLIF(:'ip_address', '')::inet, :'user_agent', :'expires_at'::timestamptz
  FROM active_staff
  WHERE NOT EXISTS (SELECT 1 FROM active_dealer)
  RETURNING expires_at
)
SELECT CASE
  WHEN EXISTS (SELECT 1 FROM active_dealer) THEN
    (SELECT json_build_object(
      'sessionToken', :'token',
      'dealerId', dealer_id::text,
      'dealerName', dealer_name,
      'username', username,
      'accountType', 'dealer_main',
      'role', 'dealer_owner',
      'canShowDailyCode', true,
      'canHandleRequests', true,
      'inviteRegisterUrl', 'https://dealer.cjwsjzyy.xyz/employee-register?dealer=' || username,
      'expiresAt', (SELECT expires_at FROM dealer_sess),
      'message', '登录成功。'
    )::text FROM active_dealer)
  WHEN EXISTS (SELECT 1 FROM active_staff) THEN
    (SELECT json_build_object(
      'sessionToken', :'token',
      'dealerId', dealer_id::text,
      'dealerName', dealer_name,
      'username', username,
      'accountType', 'dealer_staff',
      'role', role,
      'canShowDailyCode', can_show_daily_code,
      'canHandleRequests', can_handle_requests,
      'inviteRegisterUrl', null,
      'expiresAt', (SELECT expires_at FROM staff_sess),
      'message', '登录成功。'
    )::text FROM active_staff)
  WHEN EXISTS (SELECT 1 FROM dealer_auth) THEN
    (SELECT json_build_object('message',
      CASE WHEN review_status <> 'approved' THEN '经销商账号还未审核通过。' ELSE '经销商账号当前不可用。' END
    )::text FROM dealer_auth)
  WHEN EXISTS (SELECT 1 FROM staff_auth) THEN
    (SELECT json_build_object('message',
      CASE WHEN review_status <> 'approved' THEN '所属经销商还未审核通过。' ELSE '员工账号当前不可用。' END
    )::text FROM staff_auth)
  ELSE json_build_object('message', '用户名或密码错误。')::text
END;
"""
        result = _psql_json(sql, {
            "username": username,
            "password": password,
            "token": token,
            "token_hash": token_hash,
            "machine_digest": (data.get("machineDigest") or "")[:128],
            "ip_address": self.client_address[0] if self.client_address else "",
            "user_agent": self.headers.get("User-Agent", "")[:256],
            "expires_at": expires_at,
        })
        if result.returncode != 0:
            self._json(500, {"message": "登录服务保存会话失败。"})
            return
        lines = (result.stdout or "").strip().splitlines()
        response = json.loads(lines[-1]) if lines else {"message": "登录失败。"}
        if not response.get("sessionToken"):
            self._json(403, response)
            return
        self._json(200, response)

    def _handle_daily_code(self):
        dealer = self._dealer_from_bearer()
        if not dealer:
            self._json(401, {"message": "请先登录。"})
            return
        today = datetime.now(timezone.utc).astimezone().strftime("%Y%m%d")
        secret = _load_admin_env().get("AX8_ADMIN_PASSWORD", "fallback")
        digest = hashlib.sha256((dealer["dealerId"] + today + secret).encode("utf-8")).hexdigest().upper()
        code = f"{today}-{digest[:6]}"
        expires = (datetime.now(timezone.utc).replace(hour=23, minute=59, second=59, microsecond=0)).isoformat()
        self._json(200, {
            "dealerDailyCode": code,
            "expiresAt": expires,
            "scope": "dealer_daily",
            "remainingUses": 999,
            "message": "当天校验码。"
        })

    def _employee_register_page(self):
        query = parse_qs(urlparse(self.path).query)
        dealer = (query.get("dealer", [""])[0] or "").strip()
        dealer_html = _safe_text(dealer)
        dealer_js = _js_string(dealer)
        body = f"""<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>6x-cnc 经销商员工注册</title>
  <style>
    :root {{ color-scheme: light; font-family: "Microsoft YaHei UI", Arial, sans-serif; }}
    body {{ margin: 0; background: #f4f6f8; color: #111827; }}
    main {{ width: min(760px, calc(100vw - 32px)); margin: 32px auto; background: #fff; border: 1px solid #d9dee5; padding: 28px; box-sizing: border-box; }}
    header {{ border-bottom: 1px solid #e5e7eb; padding-bottom: 16px; margin-bottom: 22px; }}
    h1 {{ font-size: 24px; margin: 0 0 8px; }}
    p {{ color: #4b5563; margin: 0; line-height: 1.7; }}
    .grid {{ display: grid; grid-template-columns: 150px 1fr; gap: 14px 16px; align-items: center; }}
    label {{ font-weight: 600; }}
    input {{ width: 100%; box-sizing: border-box; height: 38px; padding: 6px 10px; font-size: 15px; border: 1px solid #b9c0ca; }}
    input[readonly] {{ background: #f3f4f6; color: #374151; }}
    .actions {{ display: flex; justify-content: flex-end; gap: 12px; margin-top: 24px; }}
    button {{ height: 40px; min-width: 120px; padding: 0 22px; font-size: 15px; border: 1px solid #9ca3af; background: #fff; cursor: pointer; }}
    button.primary {{ background: #075985; border-color: #075985; color: #fff; }}
    button:disabled {{ opacity: .55; cursor: wait; }}
    .msg {{ margin-top: 18px; min-height: 24px; white-space: pre-wrap; color: #374151; }}
    .msg.ok {{ color: #047857; }}
    .msg.err {{ color: #b91c1c; }}
    .note {{ margin-top: 20px; padding: 12px 14px; background: #f9fafb; border: 1px solid #e5e7eb; font-size: 13px; color: #4b5563; line-height: 1.6; }}
    @media (max-width: 640px) {{
      main {{ margin: 16px auto; padding: 20px; }}
      .grid {{ grid-template-columns: 1fr; gap: 7px; }}
      label {{ margin-top: 8px; }}
      .actions {{ justify-content: stretch; }}
      button {{ flex: 1; }}
    }}
  </style>
</head>
<body>
<main>
  <header>
    <h1>6x-cnc 经销商员工注册</h1>
    <p>员工账号注册后可登录经销商客户端查看终端升级需求，默认不显示每日校验码。</p>
  </header>
  <form id="form">
    <div class="grid">
      <label for="dealer">所属经销商用户名</label>
      <input id="dealer" name="dealer" value="{dealer_html}" required readonly>
      <label for="username">员工登录用户名</label>
      <input id="username" name="username" required autocomplete="username" placeholder="建议使用手机号或姓名拼音">
      <label for="displayName">员工姓名</label>
      <input id="displayName" name="displayName" required autocomplete="name">
      <label for="phone">电话</label>
      <input id="phone" name="phone" required autocomplete="tel">
      <label for="wechat">微信</label>
      <input id="wechat" name="wechat" autocomplete="off">
      <label for="password">密码</label>
      <input id="password" name="password" type="password" required autocomplete="new-password" minlength="8">
      <label for="passwordConfirm">确认密码</label>
      <input id="passwordConfirm" name="passwordConfirm" type="password" required autocomplete="new-password" minlength="8">
    </div>
    <div class="actions">
      <button type="reset">清空</button>
      <button id="submit" class="primary" type="submit">提交注册</button>
    </div>
  </form>
  <div id="msg" class="msg"></div>
  <div class="note">注册链接由经销商主账号复制给员工使用。员工账号注册后立即可登录客户端，但不显示每日校验码；后续设备升级需求会同步显示在经销商端和员工端。</div>
</main>
<script>
const form = document.getElementById('form');
const msg = document.getElementById('msg');
const submit = document.getElementById('submit');
const expectedDealer = {dealer_js};
if (!expectedDealer) {{
  msg.className = 'msg err';
  msg.textContent = '注册链接缺少经销商用户名，请让经销商主账号重新复制员工注册链接。';
  submit.disabled = true;
}}
form.addEventListener('submit', async (e) => {{
  e.preventDefault();
  const data = Object.fromEntries(new FormData(form).entries());
  msg.className = 'msg';
  if (!data.dealer) {{
    msg.className = 'msg err';
    msg.textContent = '缺少所属经销商用户名。';
    return;
  }}
  if (!data.username || !data.displayName || !data.phone) {{
    msg.className = 'msg err';
    msg.textContent = '请填写员工用户名、姓名和电话。';
    return;
  }}
  if (data.password !== data.passwordConfirm) {{
    msg.className = 'msg err';
    msg.textContent = '两次密码不一致。';
    return;
  }}
  if (data.password.length < 8) {{
    msg.className = 'msg err';
    msg.textContent = '密码至少 8 位。';
    return;
  }}
  submit.disabled = true;
  msg.textContent = '正在提交...';
  try {{
    const r = await fetch('/api/v1/dealer-user/register', {{
      method: 'POST',
      headers: {{'Content-Type': 'application/json'}},
      body: JSON.stringify(data)
    }});
    const text = await r.text();
    let body;
    try {{ body = JSON.parse(text); }} catch {{ body = {{message: text}}; }}
    msg.className = r.ok && body.success !== false ? 'msg ok' : 'msg err';
    msg.textContent = body.message || body.error || (r.ok ? '注册成功。' : '注册失败。');
    if (r.ok && body.success !== false) {{
      form.reset();
      document.getElementById('dealer').value = expectedDealer;
    }}
  }} catch (err) {{
    msg.className = 'msg err';
    msg.textContent = '网络异常，提交失败。';
  }} finally {{
    submit.disabled = false;
  }}
}});
</script>
</body>
</html>"""
        self._html(200, body)

    def _handle_employee_register(self, body):
        try:
            data = json.loads(body.decode("utf-8") or "{}")
        except Exception:
            self._json(400, {"message": "请求内容不是有效 JSON。"})
            return
        dealer_username = (data.get("dealer") or "").strip()
        username = (data.get("username") or "").strip()
        display_name = (data.get("displayName") or "").strip()
        phone = (data.get("phone") or "").strip()
        wechat = (data.get("wechat") or "").strip()
        password = data.get("password") or ""
        if not _valid_username(username):
            self._json(400, {"message": "员工用户名只能包含字母、数字、下划线、点、横杠或 @，长度 2-64。"})
            return
        if not display_name or not phone:
            self._json(400, {"message": "请填写员工姓名和电话。"})
            return
        if len(password) < 8:
            self._json(400, {"message": "密码至少 8 位。"})
            return
        sql = """
WITH dealer AS (
  SELECT dealer_id
  FROM dealers
  WHERE username = :'dealer_username'
    AND review_status = 'approved'
    AND cooperation_status = 'active'
  LIMIT 1
), ins AS (
  INSERT INTO dealer_users(public_no, dealer_id, username, display_name, phone, role, password_hash, status, can_handle_requests, can_show_daily_code)
  SELECT (SELECT COALESCE(MAX(public_no), 0) + 1 FROM dealer_users), dealer_id, :'username', :'display_name', :'phone', 'staff', crypt(:'password', gen_salt('bf')), 'active', true, false
  FROM dealer
  RETURNING dealer_user_id::text, lpad(public_no::text, 4, '0') AS public_no, username
), audit AS (
  INSERT INTO audit_events(actor_type, actor_id, event_type, entity_type, entity_id, ip_address, detail)
  SELECT 'dealer_user', username, 'dealer_user.register.submitted', 'dealer_user', dealer_user_id, NULLIF(:'ip_address', '')::inet,
         jsonb_build_object('dealerUsername', :'dealer_username', 'wechat', NULLIF(:'wechat', ''))
  FROM ins
)
SELECT CASE
  WHEN EXISTS (SELECT 1 FROM ins) THEN
    (SELECT json_build_object('success', true, 'dealerUserId', dealer_user_id, 'dealerUserNo', public_no, 'username', username, 'message', '员工账号注册成功，可以登录经销商客户端。')::text FROM ins)
  WHEN NOT EXISTS (SELECT 1 FROM dealer) THEN
    json_build_object('success', false, 'message', '所属经销商不存在、未审核通过或已停用。')::text
  ELSE json_build_object('success', false, 'message', '员工账号注册失败。')::text
END;
"""
        result = _psql_json(sql, {
            "dealer_username": dealer_username,
            "username": username,
            "display_name": display_name,
            "phone": phone,
            "wechat": wechat,
            "password": password,
            "ip_address": self.client_address[0] if self.client_address else "",
        })
        if result.returncode != 0:
            err = (result.stderr or "").lower()
            if "duplicate key" in err or "unique constraint" in err:
                self._json(409, {"message": "这个员工用户名已经注册过。"})
                return
            self._json(500, {"message": "员工注册保存失败。"})
            return
        lines = (result.stdout or "").strip().splitlines()
        response = json.loads(lines[-1]) if lines else {"success": False, "message": "员工注册失败。"}
        self._json(200 if response.get("success") else 400, response)

    def _handle_change_password(self, body):
        header = self.headers.get("Authorization", "")
        if not header.startswith("Bearer "):
            self._json(401, {"message": "请先登录。"})
            return
        token = header.removeprefix("Bearer ").strip()
        if not token:
            self._json(401, {"message": "请先登录。"})
            return
        try:
            data = json.loads(body.decode("utf-8") or "{}")
        except Exception:
            self._json(400, {"message": "请求内容不是有效 JSON。"})
            return
        old_password = data.get("oldPassword") or ""
        new_password = data.get("newPassword") or ""
        if len(new_password) < 8:
            self._json(400, {"message": "新密码至少 8 位。"})
            return
        if old_password == new_password:
            self._json(400, {"message": "新密码不能和原密码相同。"})
            return
        token_hash = hashlib.sha256(token.encode("utf-8")).hexdigest()
        sql = """
WITH sess AS (
  SELECT subject_type, subject_id
  FROM app_sessions
  WHERE token_hash = :'token_hash'
    AND revoked_at IS NULL
    AND expires_at > now()
  LIMIT 1
), dealer_upd AS (
  UPDATE dealers d
  SET password_hash = crypt(:'new_password', gen_salt('bf')), updated_at = now()
  FROM sess s
  WHERE s.subject_type = 'dealer'
    AND d.dealer_id = s.subject_id
    AND d.password_hash = crypt(:'old_password', d.password_hash)
  RETURNING d.dealer_id::text AS entity_id, d.username
), staff_upd AS (
  UPDATE dealer_users u
  SET password_hash = crypt(:'new_password', gen_salt('bf')), updated_at = now()
  FROM sess s
  WHERE s.subject_type = 'dealer_user'
    AND u.dealer_user_id = s.subject_id
    AND u.password_hash = crypt(:'old_password', u.password_hash)
  RETURNING u.dealer_user_id::text AS entity_id, u.username
), audit AS (
  INSERT INTO audit_events(actor_type, actor_id, event_type, entity_type, entity_id, ip_address, detail)
  SELECT 'account', username, 'account.password.changed', 'account', entity_id, NULLIF(:'ip_address', '')::inet,
         jsonb_build_object('clientVersion', :'client_version', 'clientBuild', :'client_build', 'machineDigest', :'machine_digest')
  FROM (
    SELECT * FROM dealer_upd
    UNION ALL
    SELECT * FROM staff_upd
  ) x
)
SELECT CASE
  WHEN EXISTS (SELECT 1 FROM dealer_upd UNION ALL SELECT 1 FROM staff_upd) THEN
    json_build_object('success', true, 'message', '密码已修改，请下次使用新密码登录。')::text
  WHEN NOT EXISTS (SELECT 1 FROM sess) THEN
    json_build_object('success', false, 'message', '登录已过期，请重新登录。')::text
  ELSE
    json_build_object('success', false, 'message', '原密码错误，修改失败。')::text
END;
"""
        result = _psql_json(sql, {
            "token_hash": token_hash,
            "old_password": old_password,
            "new_password": new_password,
            "ip_address": self.client_address[0] if self.client_address else "",
            "client_version": (data.get("clientVersion") or "")[:32],
            "client_build": (data.get("clientBuild") or "")[:32],
            "machine_digest": (data.get("machineDigest") or "")[:128],
        })
        if result.returncode != 0:
            self._json(500, {"message": "修改密码保存失败。"})
            return
        lines = (result.stdout or "").strip().splitlines()
        response = json.loads(lines[-1]) if lines else {"success": False, "message": "修改密码失败。"}
        self._json(200 if response.get("success") else 400, response)

    def _file(self, code, path, content_type=None):
        body = path.read_bytes()
        self.send_response(code)
        self.send_header("Content-Type", content_type or mimetypes.guess_type(path.name)[0] or "application/octet-stream")
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _download_url(self, host):
        public_host = "dealer.cjwsjzyy.xyz"
        return f"https://{public_host}/dealer-client/{LATEST_CLIENT_VERSION}/{LATEST_CLIENT_FILE}"

    def _handle_winremote_update_file(self, path):
        rel = path.removeprefix("/8ax-winremote/")
        if not rel or ".." in rel or rel.startswith("/") or "\\" in rel:
            self._json(400, payload("bad_path"))
            return
        root = WINREMOTE_UPDATE_ROOT.resolve()
        f = (root / rel).resolve()
        try:
            f.relative_to(root)
        except ValueError:
            self._json(400, payload("bad_path"))
            return
        if f.is_file():
            self._file(200, f)
        else:
            self._json(404, payload("not_found", {"path": path}))

    def _device_dna_from_request(self):
        query = parse_qs(urlparse(self.path).query)
        return _normalize_pl_dna(
            self.headers.get("X-8AX-Device-DNA")
            or self.headers.get("X-8AX-License-Anchor")
            or (query.get("device_dna", [""])[0] if query else "")
        )

    def _device_authorization_from_request(self):
        header = self.headers.get("X-8AX-Device-Authorization", "") or self.headers.get("X-8AX-Device-Auth", "")
        if not header:
            return None
        try:
            raw = _b64url_decode(header).decode("utf-8")
        except Exception:
            raw = header
        try:
            payload_data = json.loads(raw)
        except Exception:
            return None
        return payload_data if isinstance(payload_data, dict) else None

    def _device_dna_registered(self, dna):
        if not dna:
            return False
        sql = "SELECT CASE WHEN EXISTS (SELECT 1 FROM devices WHERE pl_device_dna = :'pl_device_dna') THEN '1' ELSE '0' END;"
        result = _psql_json(sql, {"pl_device_dna": dna})
        if result.returncode != 0:
            return False
        return (result.stdout or "").strip().splitlines()[-1:] == ["1"]

    def _device_record_by_dna(self, dna):
        if not _ensure_device_security_schema() or not dna:
            return None
        sql = """
SELECT COALESCE((
  SELECT json_build_object(
    'device_id', device_id,
    'activation_status', activation_status,
    'pl_dna_hash', pl_dna_hash,
    'device_public_key_pem', device_public_key_pem,
    'device_public_key_sha256', device_public_key_sha256,
    'device_auth_revoked_at', device_auth_revoked_at,
    'device_auth_revocation_reason', device_auth_revocation_reason
  )::text
  FROM devices
  WHERE pl_device_dna = :'pl_device_dna'
  LIMIT 1
), '') AS payload;
"""
        result = _psql_json(sql, {"pl_device_dna": dna})
        if result.returncode != 0:
            return None
        line = (result.stdout or "").strip().splitlines()[-1:] or [""]
        if not line[0]:
            return None
        try:
            record = json.loads(line[0])
        except Exception:
            return None
        return record if isinstance(record, dict) else None

    def _request_path_for_signature(self):
        parsed = urlparse(self.path)
        return parsed.path + (("?" + parsed.query) if parsed.query else "")

    def _challenge_ready(self, nonce, device_id, purpose="drive_profile_download"):
        sql = """
SELECT COALESCE((
  SELECT json_build_object('nonce', nonce, 'device_id', device_id, 'expires_at', expires_at, 'used_at', used_at)::text
  FROM device_challenges
  WHERE nonce = :'nonce'
    AND device_id = :'device_id'
    AND purpose = :'purpose'
  LIMIT 1
), '') AS payload;
"""
        result = _psql_json(sql, {"nonce": nonce, "device_id": device_id, "purpose": purpose})
        if result.returncode != 0:
            return False, "device_challenge_invalid", "device challenge lookup failed"
        line = (result.stdout or "").strip().splitlines()[-1:] or [""]
        if not line[0]:
            return False, "device_challenge_invalid", "device challenge is missing"
        try:
            row = json.loads(line[0])
        except Exception:
            return False, "device_challenge_invalid", "device challenge is invalid"
        if row.get("used_at"):
            return False, "device_challenge_invalid", "device challenge was already used"
        expires_at = _parse_iso_utc(row.get("expires_at"))
        if expires_at is None or _utc_now() > expires_at:
            return False, "device_challenge_invalid", "device challenge is expired"
        return True, "", ""

    def _mark_challenge_used(self, nonce, device_id, used_path, purpose="drive_profile_download"):
        sql = """
UPDATE device_challenges
   SET used_at = now(),
       used_path = :'used_path',
       used_request_ip = NULLIF(:'used_request_ip', '')::inet
 WHERE nonce = :'nonce'
   AND device_id = :'device_id'
   AND purpose = :'purpose'
   AND used_at IS NULL
   AND expires_at > now()
RETURNING nonce;
"""
        result = _psql_json(sql, {
            "nonce": nonce,
            "device_id": device_id,
            "purpose": purpose,
            "used_path": used_path[:512],
            "used_request_ip": self._client_ip(),
        })
        if result.returncode != 0:
            return False
        return bool((result.stdout or "").strip())

    def _verify_device_request_signature(self, dna, device_record, auth_claims, purpose="drive_profile_download"):
        device_id = str(auth_claims.get("device_id") or "").strip()
        stored_device_id = str(device_record.get("device_id") or "").strip()
        auth_pub_sha = str(auth_claims.get("device_public_key_sha256") or "").strip().lower()
        stored_pub_sha = str(device_record.get("device_public_key_sha256") or "").strip().lower()
        public_key_pem = str(device_record.get("device_public_key_pem") or "")
        if not device_id or device_id != stored_device_id:
            return False, "device_authorization_invalid", "device authorization device_id mismatch"
        if str(device_record.get("pl_dna_hash") or "").strip().lower() != _pl_dna_hash(dna).lower():
            return False, "device_authorization_invalid", "device authorization DNA hash mismatch"
        if not stored_pub_sha or not public_key_pem:
            return False, "device_public_key_missing", "device public key is not registered"
        if not auth_pub_sha or auth_pub_sha != stored_pub_sha:
            return False, "device_authorization_invalid", "device public key fingerprint mismatch"
        if device_record.get("device_auth_revoked_at"):
            return False, "device_authorization_invalid", "device authorization was revoked"
        nonce = str(self.headers.get("X-8AX-Device-Challenge", "") or "").strip()
        timestamp = str(self.headers.get("X-8AX-Device-Timestamp", "") or "").strip()
        signature_text = str(self.headers.get("X-8AX-Device-Request-Signature", "") or "").strip()
        alg = str(self.headers.get("X-8AX-Device-Request-Signature-Alg", "") or DEVICE_REQUEST_SIGNATURE_ALG).strip()
        header_device_id = str(self.headers.get("X-8AX-Device-ID", "") or "").strip()
        if not nonce or not timestamp or not signature_text:
            return False, "device_request_signature_missing", "device request signature is missing"
        if header_device_id and header_device_id != device_id:
            return False, "device_request_signature_invalid", "device request device_id mismatch"
        if alg != DEVICE_REQUEST_SIGNATURE_ALG:
            return False, "device_request_signature_invalid", "device request signature algorithm is invalid"
        parsed_time = _parse_iso_utc(timestamp)
        if parsed_time is None or abs((_utc_now() - parsed_time).total_seconds()) > 300:
            return False, "device_request_signature_invalid", "device request timestamp is outside the allowed window"
        ok, code, message = self._challenge_ready(nonce, device_id, purpose)
        if not ok:
            return False, code, message
        request_path = self._request_path_for_signature()
        payload_data = {
            "schema": DEVICE_REQUEST_SIGNATURE_SCHEMA,
            "alg": DEVICE_REQUEST_SIGNATURE_ALG,
            "device_id": device_id,
            "nonce": nonce,
            "pl_device_dna_hash": _pl_dna_hash(dna),
            "purpose": purpose,
            "request_method": self.command,
            "request_path": request_path,
            "timestamp": timestamp,
        }
        try:
            signature_bytes = _b64url_decode(signature_text)
        except Exception:
            return False, "device_request_signature_invalid", "device request signature is not valid base64url"
        if not signature_bytes or not _openssl_verify_with_public_key(public_key_pem, _canonical_json_bytes(payload_data), signature_bytes):
            return False, "device_request_signature_invalid", "device request signature verification failed"
        if not self._mark_challenge_used(nonce, device_id, request_path, purpose):
            return False, "device_challenge_invalid", "device challenge could not be consumed"
        return True, "", ""

    def _drive_profile_gate_ok(self):
        dna = self._device_dna_from_request()
        if not dna:
            self._json(401, payload("device_dna_missing", {"message": "server download requires live device DNA"}))
            return False
        device_record = self._device_record_by_dna(dna)
        if not device_record:
            self._json(403, payload("device_not_factory_registered", {"message": "驱动资料下载前必须先登记本机 DNA。"}))
            return False
        device_auth = self._device_authorization_from_request()
        if device_auth is None:
            self._json(401, payload("device_authorization_missing", {"message": "服务器下载缺少设备授权文件，请先登记本机 DNA 获取授权文件。"}))
            return False
        ok, code, message, claims = _verify_device_authorization(device_auth, dna)
        if ok:
            request_ok, request_code, request_message = self._verify_device_request_signature(
                dna, device_record, claims, "drive_profile_download"
            )
            if not request_ok:
                self._json(403, payload(request_code or "device_request_signature_invalid", {"message": request_message or "device request signature verification failed"}))
                return False
        if not ok:
            self._json(403, payload(code or "device_authorization_invalid", {"message": message or "设备授权文件校验失败。"}))
            return False
        return True

    def _remote_ssh_gate(self):
        dna = self._device_dna_from_request()
        if not dna:
            self._json(401, payload("device_dna_missing", {"message": "远程 SSH 登记需要本机 live DNA。"}))
            return None
        device_record = self._device_record_by_dna(dna)
        if not device_record:
            self._json(403, payload("device_not_factory_registered", {"message": "远程 SSH 前必须先登记本机 DNA。"}))
            return None
        device_auth = self._device_authorization_from_request()
        if device_auth is None:
            self._json(401, payload("device_authorization_missing", {"message": "远程 SSH 缺少设备授权文件。"}))
            return None
        ok, code, message, claims = _verify_device_authorization(device_auth, dna)
        if not ok:
            self._json(403, payload(code or "device_authorization_invalid", {"message": message or "设备授权文件校验失败。"}))
            return None
        permissions = [str(item) for item in claims.get("permissions", [])] if isinstance(claims.get("permissions"), list) else []
        if "remote_ssh_tunnel" not in permissions:
            self._json(403, payload("device_authorization_permission_missing", {"message": "设备授权缺少 remote_ssh_tunnel 权限。"}))
            return None
        request_ok, request_code, request_message = self._verify_device_request_signature(
            dna, device_record, claims, "remote_ssh_tunnel"
        )
        if not request_ok:
            self._json(403, payload(request_code or "device_request_signature_invalid", {"message": request_message or "设备请求签名校验失败。"}))
            return None
        return dna, device_record, claims

    def _handle_remote_ssh_register(self, body):
        gate = self._remote_ssh_gate()
        if gate is None:
            return
        _dna, device_record, claims = gate
        try:
            data = json.loads(body.decode("utf-8") or "{}") if body else {}
        except Exception:
            self._json(400, payload("bad_json", {"message": "远程 SSH 登记内容不是有效 JSON。"}))
            return
        device_id = str(claims.get("device_id") or "").strip()
        requested_device_id = str(data.get("deviceId") or data.get("device_id") or device_id).strip()
        if requested_device_id != device_id:
            self._json(403, payload("device_request_signature_invalid", {"message": "远程 SSH 设备 ID 与授权身份不一致。"}))
            return
        try:
            result = remote_ssh_gateway.register_tunnel(
                _psql_json,
                device_id,
                device_record.get("device_public_key_pem"),
                device_record.get("device_public_key_sha256"),
                self._client_ip(),
            )
        except remote_ssh_gateway.RemoteSshError as exc:
            self._json(503, payload("remote_ssh_register_failed", {"message": str(exc)}))
            return
        result.update({"success": True, "online": False, "message": "远程 SSH 隧道身份和端口已登记。"})
        self._json(200, result)

    def _handle_admin_remote_ssh_status(self):
        query = parse_qs(urlparse(self.path).query)
        device_id = str((query.get("deviceId", [""])[0] if query else "") or "").strip()
        if not re.fullmatch(r"\d{6}", device_id):
            self._json(400, payload("bad_device_id", {"message": "deviceId must be 6 digits"}))
            return
        try:
            response = remote_ssh_gateway.tunnel_status(_psql_json, device_id)
        except remote_ssh_gateway.RemoteSshError as exc:
            self._json(503, payload("remote_ssh_status_failed", {"message": str(exc)}))
            return
        self._json(200, response)

    def _handle_device_challenge(self, body):
        if not _ensure_device_security_schema():
            self._json(500, payload("device_security_schema_unavailable", {"message": "device security schema is unavailable"}))
            return
        try:
            data = json.loads(body.decode("utf-8") or "{}") if body else {}
        except Exception:
            data = {}
        query = parse_qs(urlparse(self.path).query)
        device_id = str(data.get("device_id") or (query.get("device_id", [""])[0] if query else "")).strip()
        purpose = str(data.get("purpose") or "drive_profile_download").strip()
        if not re.fullmatch(r"\d{6}", device_id or ""):
            self._json(400, payload("bad_device_id", {"message": "device_id must be 6 digits"}))
            return
        if purpose not in ("drive_profile_download", "remote_ssh_tunnel"):
            self._json(400, payload("bad_challenge_purpose", {"message": "challenge purpose is not supported"}))
            return
        sql = """
SELECT COALESCE((
  SELECT json_build_object(
    'device_id', device_id,
    'pl_dna_hash', pl_dna_hash,
    'activation_status', activation_status,
    'device_public_key_sha256', device_public_key_sha256,
    'device_auth_revoked_at', device_auth_revoked_at
  )::text
  FROM devices
  WHERE device_id = :'device_id'
  LIMIT 1
), '') AS payload;
"""
        result = _psql_json(sql, {"device_id": device_id})
        if result.returncode != 0:
            self._json(500, payload("device_challenge_db_failed", {"message": "device challenge database lookup failed"}))
            return
        line = (result.stdout or "").strip().splitlines()[-1:] or [""]
        if not line[0]:
            self._json(403, payload("device_not_factory_registered", {"message": "设备未登记：请先把本机 DNA 写入 VPS 设备数据库，再点击服务器下载。"}))
            return
        try:
            record = json.loads(line[0])
        except Exception:
            self._json(500, payload("device_challenge_record_invalid", {"message": "device challenge record is invalid"}))
            return
        if not record.get("device_public_key_sha256"):
            self._json(403, payload("device_public_key_missing", {"message": "设备公钥未登记，请重新登记本机 DNA。"}))
            return
        if record.get("device_auth_revoked_at"):
            self._json(403, payload("device_authorization_invalid", {"message": "设备授权已吊销，请联系厂家。"}))
            return
        rate_sql = """
SELECT json_build_object(
  'minute_count', count(*) FILTER (WHERE created_at > now() - interval '1 minute'),
  'hour_count', count(*) FILTER (WHERE created_at > now() - interval '1 hour')
)::text
FROM device_challenges
WHERE device_id = :'device_id';
"""
        rate = _psql_json(rate_sql, {"device_id": device_id})
        if rate.returncode == 0:
            try:
                rate_row = json.loads((rate.stdout or "{}").strip().splitlines()[-1])
            except Exception:
                rate_row = {}
            if int(rate_row.get("minute_count") or 0) >= 120 or int(rate_row.get("hour_count") or 0) >= 1000:
                self._json(429, payload("device_challenge_rate_limited", {"message": "device challenge rate limited"}))
                return
        nonce = _b64url_encode(secrets.token_bytes(32))
        expires_at = _iso_utc(_utc_now() + timedelta(minutes=5))
        insert_sql = """
INSERT INTO device_challenges(nonce, device_id, purpose, request_ip, expires_at)
VALUES (:'nonce', :'device_id', :'purpose', NULLIF(:'ip_address', '')::inet, :'expires_at'::timestamptz)
RETURNING nonce;
"""
        inserted = _psql_json(insert_sql, {
            "nonce": nonce,
            "device_id": device_id,
            "purpose": purpose,
            "ip_address": self._client_ip(),
            "expires_at": expires_at,
        })
        if inserted.returncode != 0:
            self._json(500, payload("device_challenge_create_failed", {"message": "device challenge create failed"}))
            return
        self._json(200, payload("ok", {
            "success": True,
            "ok": True,
            "deviceId": device_id,
            "nonce": nonce,
            "purpose": purpose,
            "expiresAt": expires_at,
            "signatureSchema": DEVICE_REQUEST_SIGNATURE_SCHEMA,
        }))

    def _handle_drive_profile_get(self, path):
        if not self._drive_profile_gate_ok():
            return
        prefix = "/api/v1/drive/profiles/"
        rel = path.removeprefix(prefix)
        parts = [p for p in rel.split("/") if p]
        if not parts or parts[0] not in ("public", "private"):
            self._json(404, payload("not_found", {"path": path}))
            return
        scope = parts[0]
        tail = parts[1:]
        scope_root = DRIVE_PROFILE_ROOT / scope
        if scope == "private":
            dna = self._device_dna_from_request()
            dna_hash = _pl_dna_hash(dna) if dna else ""
            if not dna_hash:
                self._json(403, payload("device_dna_missing", {"message": "private profile download requires live device DNA"}))
                return
            if not tail or not re.fullmatch(r"\d{6}", tail[0]):
                self._json(400, payload("bad_vps_distribution_id", {"message": "private profile path must include the 6-digit VPS distribution ID"}))
                return
            requested_device_id = tail[0]
            device_record = self._device_record_by_dna(dna)
            if not device_record or str(device_record.get("device_id") or "").strip() != requested_device_id:
                self._json(403, payload("device_id_dna_mismatch", {"message": "private profile path ID does not match the live device DNA"}))
                return
            private_dir = self._device_private_dir(requested_device_id, dna_hash)
            if private_dir is None:
                self._json(400, payload("bad_vps_distribution_id", {"message": "private profile path must include the 6-digit VPS distribution ID"}))
                return
            scope_root = private_dir
            tail = tail[1:]
        if tail in (["map"], ["driver_profile_map.json"]):
            file_path = scope_root / "driver_profile_map.json"
        elif tail == ["driver_profile_map.json.sha256"]:
            file_path = scope_root / "driver_profile_map.json.sha256"
        elif tail == ["package"]:
            file_path = scope_root / "package.zip"
        elif len(tail) >= 2 and tail[0] in ("driver_profiles", "source_catalogs"):
            if any(p == ".." for p in tail):
                self._json(400, payload("bad_path"))
                return
            file_path = scope_root / Path(*tail)
        else:
            self._json(404, payload("not_found", {"path": path}))
            return
        if not file_path.is_file():
            self._json(404, payload("drive_profile_file_missing", {"path": path}))
            return
        if file_path.name.endswith(".sha256"):
            content_type = "text/plain; charset=utf-8"
        else:
            content_type = "application/json; charset=utf-8" if file_path.suffix == ".json" else None
        self._file(200, file_path, content_type)

    def _handle_factory_device_register(self, body):
        try:
            data = json.loads(body.decode("utf-8") or "{}")
        except Exception:
            self._json(400, payload("bad_json", {"message": "请求内容不是有效 JSON。"}))
            return
        if not _ensure_device_security_schema():
            self._json(500, payload("device_security_schema_unavailable", {"message": "device security schema is unavailable"}))
            return
        pl_dna = _normalize_pl_dna(
            self.headers.get("X-8AX-Device-DNA")
            or self.headers.get("X-8AX-License-Anchor")
            or data.get("pl_device_dna")
            or data.get("device_dna")
        )
        if not pl_dna:
            self._json(400, payload("bad_device_dna", {"message": "PL Device DNA 缺失或格式非法。"}))
            return
        device_public_key_pem, device_public_key_sha256 = _canonical_public_key_from_request(data.get("device_public_key_pem"))
        requested_public_key_sha256 = str(data.get("device_public_key_sha256") or "").strip().lower()
        if not device_public_key_pem or not device_public_key_sha256:
            self._json(400, payload("device_public_key_missing", {"message": "设备公钥缺失或格式无效。"}))
            return
        if requested_public_key_sha256 and requested_public_key_sha256 != device_public_key_sha256:
            self._json(400, payload("device_public_key_mismatch", {"message": "设备公钥指纹与公钥内容不一致。"}))
            return
        pl_hash = _pl_dna_hash(pl_dna)
        device_id, allocation_status = _allocate_pl_dna_device_id(pl_dna, pl_hash)
        if not device_id:
            self._json(409, payload("device_id_allocation_failed", {"message": "VPS 分发ID生成失败，未写入设备登记。", "allocationStatus": allocation_status}))
            return
        device_id_source = str(data.get("device_id_source") or "uboot_dt_pl_dna").strip()[:64]
        software_version = str(data.get("software_version") or "").strip()[:64]
        hardware_digest = str(data.get("hardware_fingerprint") or data.get("hardware_digest") or "").strip()[:256]
        sql = """
WITH short_same AS (
  SELECT device_id
  FROM devices
  WHERE device_id = :'device_id'
    AND pl_dna_hash = :'pl_dna_hash'
  LIMIT 1
), short_conflict AS (
  SELECT device_id
  FROM devices
  WHERE device_id = :'device_id'
    AND COALESCE(pl_dna_hash, '') <> :'pl_dna_hash'
  LIMIT 1
), existing_same AS (
  SELECT device_id
  FROM devices
  WHERE (pl_device_dna = :'pl_device_dna' OR pl_dna_hash = :'pl_dna_hash')
    AND NOT EXISTS (SELECT 1 FROM short_same)
  ORDER BY CASE WHEN device_id LIKE 'pldna:%' OR length(device_id) > 6 THEN 0 ELSE 1 END,
           COALESCE(factory_registered_at, updated_at, created_at) DESC
  LIMIT 1
), renumber AS (
  UPDATE devices
     SET device_id = :'device_id',
         pl_device_dna = :'pl_device_dna',
         pl_dna_hash = :'pl_dna_hash',
         device_id_source = :'device_id_source',
         hardware_digest = COALESCE(NULLIF(:'hardware_digest', ''), hardware_digest),
         initial_version = COALESCE(initial_version, NULLIF(:'software_version', '')),
         current_version = COALESCE(NULLIF(:'software_version', ''), current_version),
         activation_status = CASE
             WHEN activation_status IN ('factory_ready', 'factory_registered') THEN 'factory_registered'
             ELSE activation_status
         END,
         factory_registered_at = COALESCE(factory_registered_at, now()),
         updated_at = now()
   WHERE device_id = (SELECT device_id FROM existing_same)
     AND NOT EXISTS (SELECT 1 FROM short_conflict)
  RETURNING device_id, activation_status, pl_dna_hash, device_id_source, factory_registered_at
), touch_short AS (
  UPDATE devices
     SET pl_device_dna = :'pl_device_dna',
         pl_dna_hash = :'pl_dna_hash',
         device_id_source = :'device_id_source',
         hardware_digest = COALESCE(NULLIF(:'hardware_digest', ''), hardware_digest),
         initial_version = COALESCE(initial_version, NULLIF(:'software_version', '')),
         current_version = COALESCE(NULLIF(:'software_version', ''), current_version),
         activation_status = CASE
             WHEN activation_status IN ('factory_ready', 'factory_registered') THEN 'factory_registered'
             ELSE activation_status
         END,
         factory_registered_at = COALESCE(factory_registered_at, now()),
         updated_at = now()
   WHERE device_id = (SELECT device_id FROM short_same)
     AND NOT EXISTS (SELECT 1 FROM short_conflict)
  RETURNING device_id, activation_status, pl_dna_hash, device_id_source, factory_registered_at
), upsert AS (
  INSERT INTO devices(device_id, sn, hardware_digest, initial_version, current_version, current_license_hash,
                      activation_status, pl_device_dna, pl_dna_hash, device_id_source, factory_registered_at, updated_at)
  SELECT :'device_id', NULLIF(:'sn', ''), NULLIF(:'hardware_digest', ''), NULLIF(:'software_version', ''),
         NULLIF(:'software_version', ''), NULL, 'factory_registered', :'pl_device_dna', :'pl_dna_hash',
         :'device_id_source', now(), now()
  WHERE NOT EXISTS (SELECT 1 FROM short_same)
    AND NOT EXISTS (SELECT 1 FROM existing_same)
    AND NOT EXISTS (SELECT 1 FROM short_conflict)
  ON CONFLICT (device_id) DO UPDATE
     SET pl_device_dna = EXCLUDED.pl_device_dna,
         pl_dna_hash = EXCLUDED.pl_dna_hash,
         device_id_source = EXCLUDED.device_id_source,
         hardware_digest = COALESCE(NULLIF(EXCLUDED.hardware_digest, ''), devices.hardware_digest),
         initial_version = COALESCE(devices.initial_version, EXCLUDED.initial_version),
         current_version = COALESCE(NULLIF(EXCLUDED.current_version, ''), devices.current_version),
         activation_status = CASE
             WHEN devices.activation_status IN ('factory_ready', 'factory_registered') THEN 'factory_registered'
             ELSE devices.activation_status
         END,
         factory_registered_at = COALESCE(devices.factory_registered_at, now()),
         updated_at = now()
  RETURNING device_id, activation_status, pl_dna_hash, device_id_source, factory_registered_at
), selected AS (
  SELECT * FROM renumber
  UNION ALL
  SELECT * FROM touch_short
  UNION ALL
  SELECT * FROM upsert
), audit AS (
  INSERT INTO audit_events(actor_type, actor_id, event_type, entity_type, entity_id, ip_address, detail)
  SELECT 'device', device_id, 'device.factory_dna_registered', 'device', device_id,
         NULLIF(:'ip_address', '')::inet,
         jsonb_build_object('pl_dna_hash', pl_dna_hash, 'device_id_source', device_id_source, 'source', :'source')
  FROM selected
)
SELECT COALESCE(
  (SELECT json_build_object(
    'success', true,
    'status', 'registered',
    'deviceId', device_id,
    'vpsDistributionId', CASE WHEN device_id ~ '^[0-9]{6}$' THEN device_id ELSE '' END,
    'activationStatus', activation_status,
    'deviceIdSource', device_id_source,
    'factoryRegisteredAt', factory_registered_at,
    'message', '本机 DNA 已写入 VPS 设备数据库。'
  )::text FROM selected LIMIT 1),
  (SELECT json_build_object(
    'success', false,
    'status', 'device_id_collision',
    'deviceId', :'device_id',
    'plDnaHash', :'pl_dna_hash',
    'message', '该 6 位设备ID 已被其它 DNA 占用，请人工核查。'
  )::text)
);
"""
        result = _psql_json(sql, {
            "device_id": device_id,
            "sn": str(data.get("sn") or "").strip()[:96],
            "hardware_digest": hardware_digest,
            "software_version": software_version,
            "pl_device_dna": pl_dna,
            "pl_dna_hash": pl_hash,
            "device_id_source": device_id_source,
            "allocation_status": allocation_status,
            "source": str(data.get("source") or "8ax-device").strip()[:64],
            "ip_address": self.client_address[0] if self.client_address else "",
        })
        if result.returncode != 0:
            self._json(500, payload("factory_device_register_failed", {"message": "设备 DNA 登记写入数据库失败。"}))
            return
        lines = (result.stdout or "").strip().splitlines()
        response = json.loads(lines[-1]) if lines else payload("factory_device_register_empty", {"success": False})
        for sensitive_key in ("plDnaHash", "pl_dna_hash", "plDeviceDna", "pl_device_dna", "deviceDna", "device_dna"):
            response.pop(sensitive_key, None)
        if response.get("success"):
            response_device_id = str(response.get("deviceId") or device_id).strip()
            existing_key = _psql_json(
                """
SELECT COALESCE((
  SELECT device_public_key_sha256
  FROM devices
  WHERE device_id = :'device_id'
  LIMIT 1
), '') AS device_public_key_sha256;
""",
                {"device_id": response_device_id},
            )
            if existing_key.returncode != 0:
                self._json(500, payload("device_public_key_lookup_failed", {"message": "设备公钥登记状态读取失败。"}))
                return
            existing_public_key_sha256 = ((existing_key.stdout or "").strip().splitlines()[-1:] or [""])[0].strip().lower()
            if existing_public_key_sha256 and existing_public_key_sha256 != device_public_key_sha256:
                self._json(409, payload("device_public_key_registered_mismatch", {"message": "该本机 DNA 已登记到不同设备公钥，请人工核查后再登记。"}))
                return
            key_update = _psql_json(
                """
UPDATE devices
   SET device_public_key_pem = :'device_public_key_pem',
       device_public_key_sha256 = :'device_public_key_sha256',
       device_public_key_registered_at = now(),
       device_auth_revoked_at = NULL,
       device_auth_revocation_reason = NULL,
       updated_at = now()
 WHERE device_id = :'device_id'
RETURNING device_id;
""",
                {
                    "device_id": response_device_id,
                    "device_public_key_pem": device_public_key_pem,
                    "device_public_key_sha256": device_public_key_sha256,
                },
            )
            if key_update.returncode != 0 or not (key_update.stdout or "").strip():
                self._json(500, payload("device_public_key_store_failed", {"message": "设备 DNA 已登记，但设备公钥保存失败。"}))
                return
            private_dir = self._ensure_device_private_layout(response_device_id, pl_hash)
            if private_dir is None:
                self._json(500, payload("device_private_folder_failed", {"message": "设备 DNA 已登记，但 VPS private 目录创建失败。"}))
                return
            response["devicePublicKeySha256"] = device_public_key_sha256
            response["deviceAuthorizationRequired"] = True
            response["authorizationStatus"] = "pending_factory_upload"
            response["message"] = "本机 DNA 已写入 VPS 设备数据库，等待工厂客户端生成并上传授权文件。"
        self._json(200, response)

    def do_GET(self):
        path = urlparse(self.path).path
        host = self.headers.get("Host", "")
        if path == "/healthz":
            self._json(200, payload(extra={"listen": "127.0.0.1:18080", "domain": host}))
            return
        if path == "/api/v1/device/challenge":
            self._handle_device_challenge(b"")
            return
        if path == "/api/v1/device/authorization":
            self._handle_device_authorization_download()
            return
        if path.startswith("/8ax-winremote/"):
            self._handle_winremote_update_file(path)
            return
        if path == "/admin/dealers":
            if self._admin_auth_ok():
                self._admin_dealers_page()
            return
        if path == "/api/v1/admin/dealers":
            if self._admin_auth_ok():
                self._handle_admin_dealers_json()
            return
        if path == "/api/v1/admin/dealer-users":
            if self._admin_auth_ok():
                self._handle_admin_dealer_users_json()
            return
        if path == "/api/v1/admin/upgrade-requests":
            if self._admin_auth_ok():
                self._handle_admin_upgrade_requests_json()
            return
        if path == "/api/v1/admin/devices":
            if self._admin_auth_ok():
                self._handle_admin_devices_json()
            return
        if path == "/api/v1/admin/devices/remote-ssh":
            if self._admin_auth_ok():
                self._handle_admin_remote_ssh_status()
            return
        if host.startswith(("dealer.cjwsjzyy.xyz", "dealer.3dtouch.top")) and path == "/employee-register":
            self._employee_register_page()
            return
        if host.startswith(("dealer.cjwsjzyy.xyz", "dealer.3dtouch.top")) and path in ("/", "/register", "/register.html"):
            page = DEALER_WEB_ROOT / "register.html"
            if page.exists():
                self._file(200, page, "text/html; charset=utf-8")
            else:
                self._json(503, payload("dealer_page_missing"))
            return
        if path == "/dealer-client/latest/manifest.json":
            manifest = DEALER_CLIENT_ROOT / LATEST_CLIENT_VERSION / "manifest.json"
            if manifest.exists():
                self._file(200, manifest, "application/json; charset=utf-8")
            else:
                self._json(404, payload("manifest_missing"))
            return
        if path.startswith("/dealer-client/"):
            rel = path.removeprefix("/dealer-client/")
            if ".." in rel or rel.startswith("/"):
                self._json(400, payload("bad_path"))
                return
            f = DEALER_CLIENT_ROOT / rel
            if f.is_file():
                self._file(200, f)
            else:
                self._json(404, payload("not_found", {"path": path}))
            return
        if path.startswith("/api/v1/drive/profiles/"):
            self._handle_drive_profile_get(path)
            return
        if path == "/":
            self._json(200, payload(extra={
                "domains": ["license.cjwsjzyy.xyz", "dealer.cjwsjzyy.xyz", "license.3dtouch.top", "dealer.3dtouch.top"],
                "implemented": ["/healthz", "dealer.cjwsjzyy.xyz/register", "dealer.3dtouch.top/register", "/admin/dealers", "/api/v1/dealer/register", "/api/v1/admin/dealers/review", "/api/v1/dealer-client/update-check", "/dealer-client/*"],
                "next": "deploy dealer registration/login APIs behind this gateway",
            }))
            return
        self._json(404, payload("not_found", {"path": path}))

    def do_POST(self):
        path = urlparse(self.path).path
        host = self.headers.get("Host", "")
        length = int(self.headers.get("Content-Length", "0") or "0")
        body = self.rfile.read(length) if length else b""

        if path == "/api/v1/factory/devices/register-dna":
            self._handle_factory_device_register(body)
            return
        if path == "/api/v1/device/challenge":
            self._handle_device_challenge(body)
            return
        if path == "/api/v1/device/remote-ssh/register":
            self._handle_remote_ssh_register(body)
            return
        if path == "/api/v1/dealer/register":
            try:
                data = json.loads(body.decode("utf-8") or "{}")
            except Exception:
                self._json(400, payload("bad_json", {"message": "请求内容不是有效 JSON。"}))
                return
            username = (data.get("username") or "").strip()
            dealer_name = (data.get("dealerName") or username).strip()
            contact_name = (data.get("contactName") or "").strip()
            phone = (data.get("phone") or "未填写").strip()
            wechat = (data.get("wechat") or "").strip()
            customer_contact_name = (data.get("customerContactName") or "").strip()
            customer_phone = (data.get("customerPhone") or "未填写").strip()
            customer_wechat = (data.get("customerWechat") or "").strip()
            region = (data.get("region") or "未填写").strip()
            password = data.get("password") or ""
            service_scope = (data.get("serviceScope") or "未填写").strip()
            qualification = (data.get("qualification") or "").strip()
            if not _valid_username(username):
                self._json(400, {"success": False, "error": "用户名只能包含字母、数字、下划线、点、横杠或 @，长度 2-64。"})
                return
            if not contact_name:
                self._json(400, {"success": False, "error": "请填写厂家联系用联系人。"})
                return
            if not customer_contact_name:
                self._json(400, {"success": False, "error": "请填写终端用户联系用联系人。"})
                return
            if len(password) < 8:
                self._json(400, {"success": False, "error": "密码至少 8 位。"})
                return
            sql = """
WITH ins AS (
  INSERT INTO dealers(public_no, username, dealer_name, contact_name, phone, wechat, customer_contact_name, customer_phone, customer_wechat, region, service_scope, qualification, password_hash)
  VALUES ((SELECT COALESCE(MAX(public_no), 0) + 1 FROM dealers), :'username', :'dealer_name', :'contact_name', :'phone', NULLIF(:'wechat', ''), :'customer_contact_name', :'customer_phone', NULLIF(:'customer_wechat', ''), :'region', :'service_scope', NULLIF(:'qualification', ''), crypt(:'password', gen_salt('bf')))
  RETURNING dealer_id::text, lpad(public_no::text, 4, '0') AS public_no, review_status, username
), audit AS (
  INSERT INTO audit_events(actor_type, actor_id, event_type, entity_type, entity_id, ip_address, detail)
  SELECT 'dealer', username, 'dealer.register.submitted', 'dealer', dealer_id, NULLIF(:'ip_address', '')::inet,
         jsonb_build_object('source', :'source', 'clientVersion', :'client_version', 'clientBuild', :'client_build', 'machineDigest', :'machine_digest')
  FROM ins
)
SELECT json_build_object('dealerId', dealer_id, 'dealerNo', public_no, 'reviewStatus', review_status, 'message', '注册资料已提交，等待厂家人工审核。')::text FROM ins;
"""
            result = _psql_json(sql, {
                'username': username,
                'dealer_name': dealer_name,
                'contact_name': contact_name,
                'phone': phone,
                'wechat': wechat,
                'customer_contact_name': customer_contact_name,
                'customer_phone': customer_phone,
                'customer_wechat': customer_wechat,
                'region': region,
                'password': password,
                'service_scope': service_scope,
                'qualification': qualification,
                'source': (data.get('source') or 'dealer-client')[:64],
                'client_version': (data.get('clientVersion') or '')[:32],
                'client_build': (data.get('clientBuild') or '')[:32],
                'machine_digest': (data.get('machineDigest') or '')[:128],
                'ip_address': self.client_address[0] if self.client_address else '',
            })
            if result.returncode != 0:
                err = (result.stderr or '').lower()
                if 'duplicate key' in err or 'unique constraint' in err:
                    self._json(409, {"success": False, "error": "这个用户名已经注册过。"})
                    return
                self._json(500, payload("dealer_register_failed", {"message": "注册保存失败，请联系厂家。"}))
                return
            line = (result.stdout or '').strip().splitlines()
            if not line:
                self._json(500, payload("dealer_register_empty", {"message": "注册保存失败，请联系厂家。"}))
                return
                self._json(200, json.loads(line[-1]))
            return
        if path == "/api/v1/dealer-user/login":
            self._handle_dealer_login(body)
            return
        if path == "/api/v1/dealer-user/change-password":
            self._handle_change_password(body)
            return
        if path == "/api/v1/dealer-user/register":
            self._handle_employee_register(body)
            return
        if path == "/api/v1/dealer/daily-code":
            self._handle_daily_code()
            return
        if path == "/api/v1/admin/dealers/review":
            if self._admin_auth_ok():
                self._handle_dealer_review(body)
            return
        if path == "/api/v1/admin/upgrade-requests/review":
            if self._admin_auth_ok():
                self._handle_upgrade_request_review(body)
            return
        if path == "/api/v1/admin/devices/authorization":
            if self._admin_auth_ok():
                self._handle_admin_device_authorization_upload(body)
            return
        if path == "/api/v1/admin/delete":
            if self._admin_auth_ok():
                self._handle_admin_delete(body)
            return
        if path == "/api/v1/dealer-client/update-check":
            current_version = ""
            try:
                current_version = json.loads(body.decode("utf-8") or "{}") .get("version", "")
            except Exception:
                current_version = ""
            needs_update = current_version != LATEST_CLIENT_VERSION
            self._json(200, {
                "allowContinue": not needs_update,
                "latestVersion": LATEST_CLIENT_VERSION,
                "forceUpdate": needs_update,
                "status": "update_available" if needs_update else "current",
                "message": "New 6x-cnc Dealer Client 0.1.15 is available. Version 0.1.15 moves Register to the top action area and adds Change Password after Login." if needs_update else "Current version is up to date.",
                "manifestUrl": f"https://dealer.cjwsjzyy.xyz/dealer-client/latest/manifest.json",
                "downloadUrl": self._download_url(host) if needs_update else None,
                "sha256": LATEST_CLIENT_SHA256,
                "signature": None,
                "size": LATEST_CLIENT_SIZE,
            })
            return
        if path.startswith("/api/v1/"):
            self._json(501, payload("not_implemented", {"path": path, "message": "business API is not deployed yet"}))
            return
        self._json(404, payload("not_found", {"path": path}))

    def log_message(self, fmt, *args):
        message = _redact_access_log_message(fmt % args)
        print("%s %s - %s" % (time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), self.address_string(), message), flush=True)


if __name__ == "__main__":
    host = os.environ.get("AX8_AUTH_HOST", "127.0.0.1")
    port = int(os.environ.get("AX8_AUTH_PORT", "18080"))
    httpd = ThreadingHTTPServer((host, port), Handler)
    print(f"{SERVICE} {VERSION} listening on {host}:{port}", flush=True)
    httpd.serve_forever()
