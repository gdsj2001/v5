#!/usr/bin/env python3
import os
import subprocess
import tempfile
from pathlib import Path


INSTALLER = Path(__file__).with_name('install_v5_runtime.sh')


def section(text: str, start: str, end: str) -> str:
    begin = text.index(start)
    return text[begin:text.index(end, begin)]


def main() -> int:
    text = INSTALLER.read_text(encoding='utf-8')
    stop = section(
        text, 'stop_writer_before_upgrade() {',
        '\nstop_affected_writers_before_install() {')
    dispatch = section(
        text, 'stop_affected_writers_before_install() {',
        '\nif [ "$apply" -eq 1 ] &&')
    actual_barrier = section(
        text, 'wait_publisher_actual_barrier() {',
        '\nwriter_pid_matches_path() {')

    # PIDFILE is diagnostic only: active/orphan writers are found by exact
    # /proc cmdline, including when the PIDFILE is missing or PID is reused.
    assert 'PIDFILE' not in stop
    assert '"$PROC_ROOT"/[0-9]*/cmdline' in stop
    assert 'grep -Fqx "$writer_path"' in text
    assert 'kill "$writer_pid" || return 1' in stop

    # A stop timeout fails closed. There is no KILL fallback and no trap that
    # could start the new owner after an interrupted install.
    assert 'writer did not stop before upgrade' in stop
    assert 'return 1' in stop
    assert 'kill -KILL' not in stop
    assert 'trap' not in dispatch

    preinstall = text.index('stop_affected_writers_before_install\nfi')
    install_loop = text.index('while IFS="$tab" read -r kind source destination mode extra; do')
    first_start = text.index('/etc/init.d/v5-position-status-publisher restart', install_loop)
    assert preinstall < install_loop < first_start

    # Both current writers and their shared modules select the stop barrier;
    # repeating an install is safe because an absent process is success.
    for token in (
        'manifest_position_publisher=1',
        'manifest_wcs_publisher=1',
        'v5-position-status-publisher',
        'v5-wcs-status-publisher',
        '/usr/libexec/8ax/v5_machine_status_projection.py',
        '/usr/libexec/8ax/v5_wcs_status_codec.py',
    ):
        assert token in text
    assert 'return 1\n  fi\n  for writer_cmdline' in stop

    # Spawn-only publisher services are joined once per non-UI install batch.
    # The UI/cpu-policy/all scopes reuse the UI-owned barrier and must not add
    # a second wait after each individual restart.
    assert text.count('\n    wait_publisher_actual_barrier\n') == 3
    scopes = {
        'backend': section(text, 'elif [ "$restart_scope" = "backend" ]',
                           'elif [ "$restart_scope" = "wcs" ]'),
        'wcs': section(text, 'elif [ "$restart_scope" = "wcs" ]',
                       'elif [ "$restart_scope" = "cpu_policy" ]'),
        'cpu_policy': section(text, 'elif [ "$restart_scope" = "cpu_policy" ]',
                              'elif [ "$restart_scope" = "settings" ]'),
        'settings': section(text, 'elif [ "$restart_scope" = "settings" ]',
                            '\n  else\n'),
        'all': section(text, '\n  else\n    enable_boot_services', '\n  fi\nelse\n'),
    }
    for name in ('backend', 'wcs', 'settings'):
        assert scopes[name].count('wait_publisher_actual_barrier') == 1, name
    for name in ('cpu_policy', 'all'):
        assert 'wait_publisher_actual_barrier' not in scopes[name], name
    assert scopes['backend'].index('restart_position_publisher_after_backend') < scopes['backend'].index('wait_publisher_actual_barrier')
    assert scopes['wcs'].index('/etc/init.d/v5-wcs-status-publisher restart') < scopes['wcs'].index('wait_publisher_actual_barrier')
    assert scopes['settings'].index('/etc/init.d/v5-state-publisher restart') < scopes['settings'].index('wait_publisher_actual_barrier') < scopes['settings'].index('/etc/init.d/v5-settings-actiond restart')
    for token in ('active_ini=conflict', '--pre-ui-inputs',
                  '--expected-ini "$expected_ini"', '--timeout 120'):
        assert token in actual_barrier

    # Execute the production mode detector against a synthetic /proc. Only a
    # single BUS owner reaches the canonical barrier. Pulse is cold-staged,
    # while Pulse-only, no-owner and BUS+Pulse all fail before the barrier.
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        proc_root = root / 'proc'
        barrier_log = root / 'barrier.log'
        barrier_exe = root / 'barrier.sh'
        barrier_exe.write_text(
            '#!/bin/sh\nprintf "%s\\n" "$*" >>"' +
            barrier_log.as_posix() + '"\n', encoding='utf-8')
        barrier_exe.chmod(0o755)
        bus_cmdline = proc_root / '101' / 'cmdline'
        bus_cmdline.parent.mkdir(parents=True)
        bus_cmdline.write_bytes(
            b'milltask\0/opt/8ax/v5/linuxcnc/ini/v5_bus.ini\0')

        def run_barrier() -> subprocess.CompletedProcess:
            script = f'''PROC_ROOT="{proc_root.as_posix()}"
RUNTIME_PROJECT_ROOT=/opt/8ax/v5
PUBLISHER_ACTUAL_BARRIER="{barrier_exe.as_posix()}"
{actual_barrier}
wait_publisher_actual_barrier
'''
            return subprocess.run(['sh', '-c', script], check=False,
                                  stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE)

        assert run_barrier().returncode == 0
        assert barrier_log.read_text(encoding='utf-8').strip() == (
            '--pre-ui-inputs --expected-ini '
            '/opt/8ax/v5/linuxcnc/ini/v5_bus.ini --timeout 120')
        pulse_cmdline = proc_root / '202' / 'cmdline'
        pulse_cmdline.parent.mkdir(parents=True)
        pulse_cmdline.write_bytes(
            b'milltask\0/opt/8ax/v5/linuxcnc/ini/v5_pulse.ini\0')
        barrier_log.unlink()
        conflict = run_barrier()
        assert conflict.returncode != 0
        assert b'conflicting BUS/Pulse motion owners' in conflict.stderr
        assert not barrier_log.exists()
        bus_cmdline.unlink()
        pulse_only = run_barrier()
        assert pulse_only.returncode != 0
        assert b'rejects disabled Pulse runtime mode' in pulse_only.stderr
        assert not barrier_log.exists()
        pulse_cmdline.unlink()
        no_owner = run_barrier()
        assert no_owner.returncode != 0
        assert b'found no canonical BUS/Pulse motion owner' in no_owner.stderr
        assert not barrier_log.exists()

    functions = section(
        text, 'writer_pid_matches_path() {',
        '\nstop_affected_writers_before_install() {')
    with tempfile.TemporaryDirectory() as directory:
        proc_root = Path(directory) / 'proc'
        cmdline = proc_root / '123' / 'cmdline'
        cmdline.parent.mkdir(parents=True)
        writer = '/usr/libexec/8ax/v5_position_status_publisher.py'

        def run(mode: str) -> subprocess.CompletedProcess:
            script = f'''PROC_ROOT="{proc_root.as_posix()}"
KILL_MODE={mode}
kill() {{
  [ "$KILL_MODE" = stop ] && rm -f "$PROC_ROOT/$1/cmdline"
  return 0
}}
sleep() {{ :; }}
{functions}
stop_writer_before_upgrade v5-test-writer {writer}
'''
            return subprocess.run(
                ['sh', '-c', script], check=False,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)

        # Active orphan: no PIDFILE exists, but exact cmdline is stopped.
        cmdline.write_bytes(b'python3\0' + writer.encode() + b'\0')
        assert run('stop').returncode == 0
        assert not cmdline.exists()

        # PID reuse text does not match the canonical writer and is untouched.
        cmdline.write_bytes(b'python3\0/usr/libexec/8ax/other.py\0')
        assert run('stop').returncode == 0
        assert cmdline.exists()

        # Matching writer that ignores TERM makes the pre-install barrier fail.
        cmdline.write_bytes(b'python3\0' + writer.encode() + b'\0')
        assert run('timeout').returncode != 0

        # Retry after the old process is gone is idempotent.
        cmdline.unlink()
        assert run('stop').returncode == 0

    # Retired init scripts are never executed; only exact process cleanup and
    # one-way rm-f tombstones remain.
    assert '"$retired_init" stop' not in text
    assert 'rm -f /etc/init.d/v5-rtcp-status-publisher' in text
    assert 'rm -f /etc/init.d/v5-g53-geometry-memory-owner' in text

    print('V5_RUNTIME_WRITER_UPGRADE_ORDER_OK')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
