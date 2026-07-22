#!/usr/bin/env python3
from __future__ import print_function

import json
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PUBLIC_MAP = ROOT / "board/config/drive-profiles/public/driver_profile_map.json"
PRIVATE_MAP_DIR = ROOT / "board/config/drive-profiles/private"
PARAMETER_TABLE = ROOT / "board/config/settings/drive_parameter_table.tsv"
ETHERCAT_XML = ROOT / "board/linuxcnc/hal/ethercat-conf-2ms.xml"
REQUIRED = {
    "csp_command_scheduler": ("csp_command_scheduler_raw", "0x2001", "0x3e", 0),
    "csp_velocity_feedforward_source": ("csp_velocity_feedforward_source_raw", "0x2005", "0x14", 1),
    "csp_velocity_feedforward_filter": ("csp_velocity_feedforward_filter_0p01_ms", "0x2008", "0x13", 50),
    "csp_velocity_feedforward_gain": ("csp_velocity_feedforward_gain_0p1_percent", "0x2008", "0x14", 1000),
}


def load_json(path):
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def table_rows():
    rows = {}
    for line in PARAMETER_TABLE.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) >= 3:
            rows[(parts[0].upper(), parts[1])] = parts[2]
    return rows


def normalize_hex(value, width):
    text = str(value).strip().lower()
    number = int(text, 0) if text.startswith(("0x", "+0x", "-0x")) else int(text, 16)
    return "0x%0*x" % (width, number)


def verify_profile(path, profile):
    parameters = profile.get("commissioning_parameters")
    if not isinstance(parameters, dict):
        raise AssertionError("%s:%s missing commissioning_parameters" % (path, profile.get("profile_id")))
    for name, (field, index, subindex, default_raw) in REQUIRED.items():
        item = parameters.get(name)
        if not isinstance(item, dict):
            raise AssertionError("%s:%s missing %s" % (path, profile.get("profile_id"), name))
        obj = item.get("object") or {}
        actual = (
            str(item.get("field")),
            normalize_hex(obj.get("index"), 4),
            normalize_hex(obj.get("subindex"), 2),
            int(item.get("default_raw")),
            str(item.get("data_type")),
        )
        expected = (field, normalize_hex(index, 4), normalize_hex(subindex, 2), default_raw, "uint16")
        if actual != expected:
            raise AssertionError("%s:%s:%s %r != %r" % (path, profile.get("profile_id"), name, actual, expected))


def main():
    map_paths = [PUBLIC_MAP] + sorted(PRIVATE_MAP_DIR.glob("*_driver_profile_map.json"))
    profiles = []
    for path in map_paths:
        payload = load_json(path)
        for profile in payload.get("profiles", []):
            if str(profile.get("vendor_id", "")).lower() == "0x00100000" and str(profile.get("product_code", "")).lower() == "0x000c0112":
                verify_profile(path, profile)
                profiles.append(profile)
    if not profiles:
        raise AssertionError("no SV630N profiles")

    rows = table_rows()
    tree = ET.parse(str(ETHERCAT_XML))
    slaves = tree.getroot().findall("./master/slave")
    if len(slaves) != 5:
        raise AssertionError("expected 5 slaves, got %d" % len(slaves))
    profile_parameters = profiles[-1]["commissioning_parameters"]
    for slave in slaves:
        position = int(slave.get("idx"))
        key = "SLAVE_%d" % position
        configs = {}
        for config in slave.findall("sdoConfig"):
            index = normalize_hex(config.get("idx"), 4)
            subindex = normalize_hex(config.get("subIdx"), 2)
            raw = config.find("sdoDataRaw")
            configs[(index, subindex)] = str(raw.get("data") if raw is not None else "").lower()
        for name, item in profile_parameters.items():
            field = item["field"]
            raw_value = int(str(rows.get((key, field), item["default_raw"])), 0)
            obj = item["object"]
            object_key = (normalize_hex(obj["index"], 4), normalize_hex(obj["subindex"], 2))
            expected_raw = int(raw_value).to_bytes(2, byteorder="little", signed=False).hex()
            if configs.get(object_key) != expected_raw:
                raise AssertionError("%s %s expected %s, got %s" % (key, object_key, expected_raw, configs.get(object_key)))
    print("v5 EtherCAT commissioning projection ok: profiles=%d slaves=%d" % (len(profiles), len(slaves)))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print("FAIL: %s" % exc, file=sys.stderr)
        sys.exit(1)
