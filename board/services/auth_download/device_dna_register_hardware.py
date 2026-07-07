from __future__ import annotations

import re
import os
import struct
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional
from device_dna_register_auth import DnaRegisterError
DEFAULT_CHOSEN_DIR = '/proc/device-tree/chosen'
EXPECTED_DNA_BITS = 57
EXPECTED_MAGIC = 0x444E4130
EXPECTED_VERSION = 0x00010000
EXPECTED_STATUS = 0x00000007
DNA_HI_MASK = (1 << (EXPECTED_DNA_BITS - 32)) - 1
DNA_VALUE_RE = re.compile(r'^(?:0x)?([0-9a-fA-F]{16})$')

def read_text(path: str) -> str:
    try:
        return Path(path).read_text(encoding="utf-8", errors="ignore").strip()
    except FileNotFoundError:
        return ""

def chosen_dir() -> Path:
    return Path(os.environ.get("RE_V5_DEVICE_TREE_CHOSEN_DIR", DEFAULT_CHOSEN_DIR))

def read_prop(prefix: str, name: str) -> bytes:
    return (chosen_dir() / ("%s%s" % (prefix, name))).read_bytes()

def read_fdt_string(prefix: str, name: str) -> str:
    return read_prop(prefix, name).rstrip(b"\x00").decode("ascii", errors="strict").strip()

def read_fdt_u32(prefix: str, name: str) -> int:
    raw = read_prop(prefix, name)
    stripped = raw.rstrip(b"\x00").strip()
    if len(raw) == 4:
        return struct.unpack(">I", raw)[0]
    return int(stripped.decode("ascii"), 0)

def dna_prefixes() -> List[str]:
    override = os.environ.get("RE_V5_PL_DNA_PROP_PREFIX", "").strip()
    if override:
        return [override]
    return ["reb,pl-dna-", "8ax,pl-dna-"]

def read_live_dna() -> Dict[str, Any]:
    errors: List[str] = []
    for prefix in dna_prefixes():
        try:
            source = read_fdt_string(prefix, "source")
            value_text = read_fdt_string(prefix, "value")
            match = DNA_VALUE_RE.fullmatch(value_text)
            if not match:
                raise ValueError("invalid DNA value: %r" % value_text)
            value_no_prefix = match.group(1).upper()
            dna_value = int(value_no_prefix, 16)
            if dna_value >= (1 << EXPECTED_DNA_BITS):
                raise ValueError("DNA value exceeds %d bits" % EXPECTED_DNA_BITS)
            bits = read_fdt_u32(prefix, "len")
            magic = read_fdt_u32(prefix, "magic")
            version = read_fdt_u32(prefix, "version")
            status = read_fdt_u32(prefix, "status")
            lo = read_fdt_u32(prefix, "lo")
            hi = read_fdt_u32(prefix, "hi")
            if bits != EXPECTED_DNA_BITS:
                raise ValueError("unexpected DNA bit count: %s" % bits)
            if magic != EXPECTED_MAGIC:
                raise ValueError("unexpected DNA magic: 0x%08X" % magic)
            if version != EXPECTED_VERSION:
                raise ValueError("unexpected DNA version: 0x%08X" % version)
            if status != EXPECTED_STATUS:
                raise ValueError("unexpected DNA status: 0x%08X" % status)
            raw_value = ((hi & DNA_HI_MASK) << 32) | lo
            if raw_value != dna_value:
                raise ValueError("DNA register/value mismatch")
            return {
                "value": "0x%016X" % dna_value,
                "source": source or "uboot_dt_pl_dna",
                "type": "zynq7000_pl_device_dna_57",
                "bits": bits,
                "prefix": prefix,
                "status_hex": "0x%08X" % status,
            }
        except Exception as exc:
            errors.append("%s:%s" % (prefix, exc))
    raise DnaRegisterError("DNA_READ_FAILED", "本机 PL Device DNA 读取失败: " + "; ".join(errors[-3:]))
