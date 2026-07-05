"""Private device binding validation for VPS admin publish endpoints."""

from __future__ import annotations

import json
import os
import subprocess
from http import HTTPStatus
from pathlib import Path
from typing import Callable
from typing import TypeVar


POSTGRES_ENV_FILE = Path(os.environ.get("AX8_AUTH_POSTGRES_ENV_FILE", "/opt/8ax-auth/secrets/postgres-8ax-auth.env"))
TError = TypeVar("TError", bound=Exception)
ErrorFactory = Callable[[HTTPStatus, str], TError]


def load_db_env() -> dict[str, str]:
    values: dict[str, str] = {}
    if POSTGRES_ENV_FILE.exists():
        for line in POSTGRES_ENV_FILE.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            values[key.strip().lstrip("\ufeff")] = value.strip().strip("\"").strip("'")
    return values


def psql_json(sql: str, variables: dict[str, str]) -> subprocess.CompletedProcess[str]:
    cfg = load_db_env()
    env = os.environ.copy()
    env["PGPASSWORD"] = cfg.get("AX8_AUTH_DB_PASSWORD", "")
    cmd = [
        "/usr/bin/psql",
        "-h",
        cfg.get("AX8_AUTH_DB_HOST", "127.0.0.1"),
        "-p",
        cfg.get("AX8_AUTH_DB_PORT", "5432"),
        "-U",
        cfg.get("AX8_AUTH_DB_USER", "8ax_auth_app"),
        "-d",
        cfg.get("AX8_AUTH_DB_NAME", "8ax_auth"),
        "-X",
        "-q",
        "-t",
        "-A",
        "--no-psqlrc",
    ]
    for key, value in variables.items():
        cmd += ["-v", f"{key}={value if value is not None else ''}"]
    return subprocess.run(cmd, env=env, text=True, input=sql, capture_output=True, timeout=15)


def validate_private_device_binding(private_id: str, private_hash: str, error_factory: ErrorFactory[TError]) -> None:
    sql = """
SELECT COALESCE((
  SELECT json_build_object('plDnaHash', COALESCE(pl_dna_hash, ''))::text
  FROM devices
  WHERE device_id = :'device_id'
  LIMIT 1
), '') AS payload;
"""
    result = psql_json(sql, {"device_id": private_id})
    if result.returncode != 0:
        raise error_factory(HTTPStatus.SERVICE_UNAVAILABLE, "private device binding validation is unavailable")
    line = (result.stdout or "").strip().splitlines()[-1:] or [""]
    if not line[0]:
        raise error_factory(HTTPStatus.NOT_FOUND, "privateId is not registered on VPS")
    try:
        record = json.loads(line[0])
    except json.JSONDecodeError as exc:
        raise error_factory(HTTPStatus.SERVICE_UNAVAILABLE, "private device binding record is invalid") from exc
    if str(record.get("plDnaHash") or "").strip().lower() != private_hash.lower():
        raise error_factory(HTTPStatus.FORBIDDEN, "privateId and privateHash do not match the VPS device record")
