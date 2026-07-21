from __future__ import annotations

import ipaddress
import os
import threading
import time
from pathlib import Path

_CPU_SAMPLE_LOCK = threading.Lock()
_CPU_SAMPLE_PREVIOUS = None
_CPU_SAMPLE_GENERATION = 0

def now_ms() -> int:
    return int(time.time() * 1000)


def parse_allow_cidrs(text: str):
    networks = []
    for item in (text or "").split(","):
        token = item.strip()
        if not token:
            continue
        if token == "*":
            return None
        networks.append(ipaddress.ip_network(token, strict=False))
    return networks


def peer_allowed(peer: str, networks) -> bool:
    if networks is None:
        return True
    try:
        addr = ipaddress.ip_address(peer)
    except ValueError:
        return False
    return any(addr in network for network in networks)


def system_metrics(cpu_usage=None, sample_cpu_if_missing=True) -> dict:
    if cpu_usage is None:
        cpu_usage = sample_cpu_usage() if sample_cpu_if_missing else {
            "cpu0_percent": None,
            "cpu1_percent": None,
            "cpu_sample_generation": 0,
            "cpu_sample_monotonic_ns": 0,
        }
    memory_used, memory_total = memory_used_total()
    disk_used, disk_total = disk_used_total()
    return {
        "cpu0_percent": cpu_usage["cpu0_percent"],
        "cpu1_percent": cpu_usage["cpu1_percent"],
        "cpu_sample_generation": cpu_usage["cpu_sample_generation"],
        "cpu_sample_monotonic_ns": cpu_usage["cpu_sample_monotonic_ns"],
        "memory_percent": percent_used(memory_used, memory_total),
        "disk_percent": percent_used(disk_used, disk_total),
        "memory_used_bytes": memory_used,
        "memory_total_bytes": memory_total,
        "disk_used_bytes": disk_used,
        "disk_total_bytes": disk_total,
    }


def sample_cpu_usage() -> dict:
    global _CPU_SAMPLE_PREVIOUS
    global _CPU_SAMPLE_GENERATION
    current = cpu_samples_snapshot()
    now_ns = time.monotonic_ns()
    with _CPU_SAMPLE_LOCK:
        previous = _CPU_SAMPLE_PREVIOUS
        _CPU_SAMPLE_PREVIOUS = current
        _CPU_SAMPLE_GENERATION += 1
        result = {
            "cpu0_percent": None,
            "cpu1_percent": None,
            "cpu_sample_generation": _CPU_SAMPLE_GENERATION,
            "cpu_sample_monotonic_ns": now_ns,
        }
        if not isinstance(previous, dict):
            return result
        for name in ("cpu0", "cpu1"):
            before = previous.get(name)
            after = current.get(name)
            if not isinstance(before, dict) or not isinstance(after, dict):
                continue
            total_delta = int(after["total"]) - int(before["total"])
            idle_delta = int(after["idle"]) - int(before["idle"])
            if total_delta <= 0 or idle_delta < 0:
                continue
            busy_delta = max(0, min(total_delta, total_delta - idle_delta))
            result[name + "_percent"] = round(
                (busy_delta / total_delta) * 100.0, 3
            )
        return result


def cpu_samples_snapshot() -> dict:
    samples = {}
    for name in ("cpu0", "cpu1"):
        sample = read_cpu_sample(name)
        if sample is None:
            samples[name] = None
        else:
            total, idle = sample
            samples[name] = {"total": total, "idle": idle}
    return samples


def read_cpu_sample(name: str) -> tuple[int, int] | None:
    try:
        for line in Path("/proc/stat").read_text(encoding="ascii").splitlines():
            parts = line.split()
            if parts and parts[0] == name:
                values = [int(part) for part in parts[1:]]
                total = sum(values)
                idle = values[3] + (values[4] if len(values) > 4 else 0)
                if total <= 0:
                    return None
                return total, idle
    except OSError:
        return None
    return None


def read_proc_stat_ticks(path: Path) -> int | None:
    try:
        text = path.read_text(encoding="ascii", errors="ignore")
    except OSError:
        return None
    end = text.rfind(")")
    if end < 0:
        return None
    fields = text[end + 2:].split()
    if len(fields) <= 12:
        return None
    try:
        return int(fields[11]) + int(fields[12])
    except ValueError:
        return None


def read_status_field(path: Path, field: str) -> str:
    try:
        for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
            if line.startswith(field + ":"):
                return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return ""


def process_diagnostics() -> dict:
    pid = os.getpid()
    task_dir = Path(f"/proc/{pid}/task")
    threads = []
    try:
        tasks = sorted(task_dir.glob("[0-9]*"), key=lambda item: int(item.name))
    except OSError:
        tasks = []
    for task in tasks:
        try:
            tid = int(task.name)
        except ValueError:
            continue
        try:
            comm = (task / "comm").read_text(encoding="utf-8", errors="ignore").strip()
        except OSError:
            comm = ""
        threads.append({
            "tid": tid,
            "comm": comm,
            "cpu_ticks": read_proc_stat_ticks(task / "stat"),
            "cpus_allowed_list": read_status_field(task / "status", "Cpus_allowed_list"),
        })
    return {
        "pid": pid,
        "cpu_ticks": read_proc_stat_ticks(Path(f"/proc/{pid}/stat")),
        "cpus_allowed_list": read_status_field(Path(f"/proc/{pid}/status"), "Cpus_allowed_list"),
        "threads": threads,
    }


def memory_used_total() -> tuple[int | None, int | None]:
    total = None
    available = None
    try:
        for line in Path("/proc/meminfo").read_text(encoding="ascii").splitlines():
            if line.startswith("MemTotal:"):
                total = int(line.split()[1]) * 1024
            elif line.startswith("MemAvailable:"):
                available = int(line.split()[1]) * 1024
    except OSError:
        return None, None
    if total is None:
        return None, None
    used = max(0, total - (available or 0))
    return used, total


def percent_used(used: int | None, total: int | None):
    if not used or not total:
        return None
    return round((used / total) * 100.0, 1)


def disk_used_total() -> tuple[int | None, int | None]:
    try:
        st = os.statvfs("/")
    except OSError:
        return None, None
    total = st.f_blocks * st.f_frsize
    free = st.f_bfree * st.f_frsize
    return max(0, total - free), total
