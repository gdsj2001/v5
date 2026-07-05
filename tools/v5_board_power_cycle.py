#!/usr/bin/env python3
r"""Power-cycle the Z20 board through the USB relay.

This helper is intentionally self-contained under this repository's tools
directory so v5 can be moved without relying on a legacy external tool tree. It
only controls board power relay CH4 by default: CH4 ON means board power off,
CH4 OFF means board power on. When --boot-mode is set, it changes boot strap
relays while the board is powered off: SD is CH1 OFF/CH2 OFF, QSPI is
CH1 ON/CH2 OFF.
"""

from __future__ import annotations

import argparse
import json
import socket
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:  # pragma: no cover - host dependency
    print("Missing dependency: pyserial", file=sys.stderr)
    print("Install with: py -m pip install pyserial", file=sys.stderr)
    raise SystemExit(1) from exc


PORT_MAP_PATH = Path(__file__).resolve().with_name("z20_port_map.json")
RELAY_BAUD = 9600
RELAY_QUERY_COMMANDS = {
    1: b"\xA0\x01\x05\xA6",
    2: b"\xA0\x02\x05\xA7",
    3: b"\xA0\x03\x05\xA8",
    4: b"\xA0\x04\x05\xA9",
}
RELAY_COMMANDS = {
    1: {"on": b"\xA0\x01\x03\xA4", "off": b"\xA0\x01\x02\xA3"},
    2: {"on": b"\xA0\x02\x03\xA5", "off": b"\xA0\x02\x02\xA4"},
    3: {"on": b"\xA0\x03\x03\xA6", "off": b"\xA0\x03\x02\xA5"},
    4: {"on": b"\xA0\x04\x03\xA7", "off": b"\xA0\x04\x02\xA6"},
}


def stamp() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%S%z")


def normalize_port(port: str | None) -> str | None:
    if not port:
        return None
    return port.strip().upper()


def hex_bytes(payload: bytes) -> str:
    return payload.hex(" ")


def load_port_map(path: Path = PORT_MAP_PATH) -> dict[str, Any]:
    if not path.exists():
        return {}
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return {}


def available_port_names() -> set[str]:
    return {normalize_port(item.device) or "" for item in list_ports.comports()}


def valid_relay_response(channel: int, data: bytes) -> bool:
    if len(data) < 4:
        return False
    data = data[:4]
    return data[0] == 0xA0 and data[1] == channel and data[3] == ((data[0] + data[1] + data[2]) & 0xFF)


def probe_relay(port: str, baud: int = RELAY_BAUD, timeout: float = 0.25) -> dict[str, Any]:
    responses: dict[str, str] = {}
    ok_channels: list[int] = []
    try:
        with serial.Serial(port, baudrate=baud, timeout=timeout, write_timeout=timeout) as ser:
            ser.reset_input_buffer()
            ser.reset_output_buffer()
            for channel, payload in RELAY_QUERY_COMMANDS.items():
                ser.write(payload)
                ser.flush()
                time.sleep(0.08)
                data = ser.read(16)
                responses[str(channel)] = hex_bytes(data)
                if valid_relay_response(channel, data):
                    ok_channels.append(channel)
    except Exception as exc:
        return {
            "port": normalize_port(port),
            "is_relay": False,
            "ok_channels": ok_channels,
            "responses": responses,
            "error": f"{type(exc).__name__}: {exc}",
        }
    return {
        "port": normalize_port(port),
        "is_relay": len(ok_channels) >= 2,
        "ok_channels": ok_channels,
        "responses": responses,
    }


def resolve_relay_port(explicit: str | None) -> tuple[str, dict[str, Any]]:
    explicit = normalize_port(explicit)
    present = available_port_names()
    saved = load_port_map()
    saved_relay = normalize_port(saved.get("relay_port"))
    if explicit:
        if explicit not in present:
            raise RuntimeError(f"Relay port {explicit} is not present; present={sorted(present)}")
        return explicit, {"source": "explicit", "present": sorted(present)}
    if saved_relay and saved_relay in present:
        return saved_relay, {"source": "tools/z20_port_map.json", "present": sorted(present), "saved": saved}
    probes = [probe_relay(port) for port in sorted(present)]
    relay = None
    for probe in sorted(probes, key=lambda item: len(item.get("ok_channels", [])), reverse=True):
        if probe.get("is_relay"):
            relay = probe
            break
    if relay and relay.get("port"):
        return str(relay["port"]), {"source": "auto-discovery", "present": sorted(present), "relay_probes": probes}
    raise RuntimeError(f"Unable to resolve relay port; present={sorted(present)} probes={probes}")


def send_state(ser: serial.Serial, channel: int, state: str) -> dict[str, Any]:
    payload = RELAY_COMMANDS[channel][state]
    ser.write(payload)
    ser.flush()
    time.sleep(0.12)
    return {"channel": channel, "state": state, "payload": hex_bytes(payload), "wall": stamp()}


def run_command(args: list[str], timeout_s: float) -> dict[str, Any]:
    try:
        completed = subprocess.run(
            args,
            text=True,
            encoding="utf-8",
            errors="replace",
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout_s,
            check=False,
        )
        return {
            "args": args,
            "returncode": completed.returncode,
            "stdout_tail": completed.stdout[-4000:],
        }
    except Exception as exc:
        return {"args": args, "returncode": -1, "error": f"{type(exc).__name__}: {exc}"}


def pre_poweroff_sync(target: str, timeout_s: float) -> dict[str, Any]:
    if not target:
        return {"skipped": True, "reason": "empty_target"}
    return run_command(["ssh", "-o", "ConnectTimeout=5", target, "sync"], timeout_s=timeout_s)


def tcp_ready(host: str, port: int, timeout_s: float) -> bool:
    try:
        with socket.create_connection((host, port), timeout=timeout_s):
            return True
    except OSError:
        return False


def wait_network(host: str, port: int, timeout_s: float, interval_s: float) -> dict[str, Any]:
    start = time.monotonic()
    attempts = 0
    while time.monotonic() - start < timeout_s:
        attempts += 1
        if tcp_ready(host, port, timeout_s=min(3.0, interval_s)):
            return {"ok": True, "elapsed_s": round(time.monotonic() - start, 3), "attempts": attempts}
        time.sleep(interval_s)
    return {"ok": False, "elapsed_s": round(time.monotonic() - start, 3), "attempts": attempts}


def wait_ssh(target: str, timeout_s: float, interval_s: float) -> dict[str, Any]:
    if not target:
        return {"skipped": True, "reason": "empty_target"}
    start = time.monotonic()
    attempts = 0
    while time.monotonic() - start < timeout_s:
        attempts += 1
        result = run_command(["ssh", "-o", "ConnectTimeout=5", target, "true"], timeout_s=10.0)
        if result["returncode"] == 0:
            return {"ok": True, "elapsed_s": round(time.monotonic() - start, 3), "attempts": attempts}
        time.sleep(interval_s)
    return {"ok": False, "elapsed_s": round(time.monotonic() - start, 3), "attempts": attempts}


def apply_boot_mode(ser: serial.Serial, mode: str) -> list[dict[str, Any]]:
    if mode == "none":
        return []
    if mode == "sd":
        return [
            send_state(ser, 1, "off"),
            send_state(ser, 2, "off"),
        ]
    if mode == "qspi":
        return [
            send_state(ser, 1, "on"),
            send_state(ser, 2, "off"),
        ]
    raise ValueError(f"unsupported boot mode: {mode}")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Power-cycle the board through USB relay CH4.")
    parser.add_argument("--port", "--relay-port", dest="port", default="", help="Relay COM port. Defaults to tools/z20_port_map.json or auto-discovery.")
    parser.add_argument("--console-port", default="", help="Accepted for compatibility; console logging is handled separately.")
    parser.add_argument("--baud", type=int, default=RELAY_BAUD)
    parser.add_argument("--serial-timeout", type=float, default=0.5)
    parser.add_argument("--power-off-seconds", type=float, default=2.0)
    parser.add_argument("--strap-settle-seconds", type=float, default=0.2)
    parser.add_argument("--post-power-on-wait-s", type=float, default=1.0)
    parser.add_argument("--boot-mode", choices=("none", "sd", "qspi"), default="none", help="Optionally set boot strap relays while power is off.")
    parser.add_argument("--target", default="z20-board", help="SSH target used for optional pre-poweroff sync and SSH readiness.")
    parser.add_argument("--net-check-host", default="192.168.1.221")
    parser.add_argument("--net-check-port", type=int, default=22)
    parser.add_argument("--net-check-timeout", type=float, default=90.0)
    parser.add_argument("--net-check-interval", type=float, default=2.0)
    parser.add_argument("--net-check-ssh-target", default="z20-board")
    parser.add_argument("--skip-net-check", action="store_true")
    parser.add_argument("--skip-pre-poweroff-sync", action="store_true")
    parser.add_argument("--pre-poweroff-sync-timeout", type=float, default=10.0)
    parser.add_argument("--json-out", type=Path, default=None)
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    result: dict[str, Any] = {
        "ok": False,
        "started_at": stamp(),
        "relay_channel": 4,
        "power_off_seconds": args.power_off_seconds,
        "boot_mode": args.boot_mode,
    }
    try:
        relay_port, port_meta = resolve_relay_port(args.port)
        result["relay_port"] = relay_port
        result["port_meta"] = port_meta
        if not args.skip_pre_poweroff_sync:
            result["pre_poweroff_sync"] = pre_poweroff_sync(args.target, args.pre_poweroff_sync_timeout)
        steps = []
        with serial.Serial(relay_port, baudrate=args.baud, timeout=args.serial_timeout, write_timeout=args.serial_timeout) as ser:
            ser.reset_input_buffer()
            ser.reset_output_buffer()
            steps.append(send_state(ser, 4, "on"))
            time.sleep(args.power_off_seconds)
            steps.extend(apply_boot_mode(ser, args.boot_mode))
            if args.boot_mode != "none":
                time.sleep(max(0.0, args.strap_settle_seconds))
            steps.append(send_state(ser, 4, "off"))
        result["relay_steps"] = steps
        time.sleep(max(0.0, args.post_power_on_wait_s))
        if args.skip_net_check:
            result["network"] = {"skipped": True}
            result["ssh"] = {"skipped": True}
            result["ok"] = True
        else:
            result["network"] = wait_network(
                args.net_check_host,
                args.net_check_port,
                args.net_check_timeout,
                args.net_check_interval,
            )
            result["ssh"] = wait_ssh(
                args.net_check_ssh_target,
                args.net_check_timeout,
                args.net_check_interval,
            )
            result["ok"] = bool(result["network"].get("ok") and result["ssh"].get("ok"))
    except Exception as exc:
        result["error"] = f"{type(exc).__name__}: {exc}"
    result["finished_at"] = stamp()
    text = json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True)
    print(text)
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(text + "\n", encoding="utf-8")
    return 0 if result.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
