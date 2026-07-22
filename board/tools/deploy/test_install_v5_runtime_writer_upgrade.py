#!/usr/bin/env python3
import os
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path


INSTALLER = Path(__file__).with_name('install_v5_runtime.sh')
MANIFEST = Path(__file__).resolve().parents[2] / 'config/deploy/v5_runtime_deploy_manifest.tsv'


def section(text: str, start: str, end: str) -> str:
    begin = text.index(start)
    return text[begin:text.index(end, begin)]


STATE_SCANNER_EDGES = (
    'manifest_state_publisher=0',
    '/usr/libexec/8ax/v5_state_publisher|\\\n'
    '    /etc/init.d/v5-state-publisher)\n'
    '      manifest_state_publisher=1',
)
STATE_DISPATCH_EDGES = (
    'if [ "$manifest_state_publisher" -eq 1 ]; then',
    'v5-state-publisher \\',
    '/usr/libexec/8ax/v5_state_publisher',
)


def audit_state_upgrade(text: str) -> None:
    scanner = section(text, 'manifest_position_publisher=0',
                      '\ncase "$restart_scope_requested" in')
    dispatch = section(text, 'stop_affected_writers_before_install() {',
                       '\nstop_shm_abi_domain_before_install() {')
    for token in STATE_SCANNER_EDGES:
        assert token in scanner, 'STATE_WRITER_SELECTOR_MISSING:' + token
    for token in STATE_DISPATCH_EDGES:
        assert token in dispatch, 'STATE_WRITER_DISPATCH_MISSING:' + token


def audit_parameter_table_merge_behavior(text: str) -> None:
    transaction = section(
        text, 'parameter_table_transaction_cleanup() {',
        '\ntab=$(printf')

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        template = root / 'template.tsv'
        destination = root / 'board.tsv'
        snapshot_root = root / 'vm-build' / 'temp_parameter_snapshot'

        def run_merge(*, fail_after: bool = False) -> subprocess.CompletedProcess:
            completion = 'false' if fail_after else 'parameter_table_transaction_complete'
            script = f'''set -eu
parameter_table_snapshot_root={shlex.quote(snapshot_root.as_posix())}
parameter_table_transaction_dir=
parameter_table_transaction_active=0
parameter_table_transaction_count=0
parameter_table_transaction_entry=
PYTHON_EXE={shlex.quote(Path(sys.executable).as_posix())}
python3() {{ "$PYTHON_EXE" "$@"; }}
{transaction}
merge_runtime_seed_tsv {shlex.quote(template.as_posix())} {shlex.quote(destination.as_posix())} 0666
{completion}
'''
            return subprocess.run(
                ['sh'], input=script.encode('utf-8'), check=False,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)

        def assert_snapshot_clean() -> None:
            assert not snapshot_root.exists() or not any(snapshot_root.iterdir())

        template.write_text(
            '# schema=v5.settings.parameter_table.tsv.v1\n'
            'X\tencoder_bits\t18\n'
            'C\tencoder_bits\t18\n', encoding='utf-8')
        destination.write_text(
            '# schema=v5.settings.parameter_table.tsv.v1\n'
            'C\tencoder_bits\t19\n'
            'X\tencoder_bits\t20\n'
            'C\tegear_numerator\t4096\n', encoding='utf-8')
        merged = run_merge()
        assert merged.returncode == 0, merged.stderr.decode(errors='replace')
        assert destination.read_text(encoding='utf-8') == (
            '# schema=v5.settings.parameter_table.tsv.v1\n'
            'X\tencoder_bits\t20\n'
            'C\tencoder_bits\t19\n')
        assert 'egear_numerator' not in destination.read_text(encoding='utf-8')
        assert_snapshot_clean()

        for invalid_board, expected_error in (
            ('X\tencoder_bits\t18\textra\n',
             'malformed board parameter table row'),
            ('X\tencoder_bits\t18\nX\tencoder_bits\t19\n',
             'duplicate board parameter key'),
        ):
            destination.write_text(invalid_board, encoding='utf-8')
            before = destination.read_bytes()
            rejected = run_merge()
            assert rejected.returncode != 0
            assert expected_error.encode() in rejected.stderr
            assert destination.read_bytes() == before
            assert_snapshot_clean()

        destination.write_text(
            '# schema=v5.settings.parameter_table.tsv.v1\n'
            'X\tencoder_bits\t21\n'
            'C\tencoder_bits\t22\n'
            'RETIRED\twrite_status\told\n', encoding='utf-8')
        before = destination.read_bytes()
        later_failure = run_merge(fail_after=True)
        assert later_failure.returncode != 0
        assert destination.read_bytes() == before
        assert b'restored parameter owner after failed deploy' in later_failure.stderr
        assert_snapshot_clean()

        destination.unlink()
        absent_failure = run_merge(fail_after=True)
        assert absent_failure.returncode != 0
        assert not destination.exists()
        assert_snapshot_clean()


def main() -> int:
    text = INSTALLER.read_text(encoding='utf-8')
    manifest = MANIFEST.read_text(encoding='utf-8')
    audit_state_upgrade(text)
    audit_parameter_table_merge_behavior(text)
    for token in (
        'disable_unconditional_ethercat_autostart',
        'rm -f "$dir"/S??ethercat',
        'enable_runtime_startup_boot_graph',
        'disable_boot_service "$service"',
        'enable_auxiliary_boot_service v5-runtime-startup 05 14',
    ):
        assert token in text, 'MODE_SELECTED_BOOT_ORDER_MISSING:' + token
    for retired in (
        'enable_boot_service()',
        'enable_boot_service_raw()',
        'enable_boot_service v5-linuxcnc-command-gate',
        'enable_boot_service v5-position-status-publisher',
        'enable_boot_service v5-wcs-status-publisher',
        'enable_boot_service v5-state-publisher',
        'enable_boot_service v5-ui-relay',
        'enable_boot_service v5-settings-actiond',
    ):
        assert retired not in text, 'RETIRED_PER_SERVICE_BOOT_EDGE_SURVIVED:' + retired
    assert 'manifest_state_only=1' in text
    assert 'restart_scope=state' in text
    assert 'State-publisher restart scope requires a non-empty State-publisher-only manifest' in text
    state_scope = section(
        text, 'elif [ "$restart_scope" = "state" ]',
        'elif [ "$restart_scope" = "actiond" ]')
    assert '/etc/init.d/v5-state-publisher restart' in state_scope
    assert 'v5-ui-relay' not in state_scope
    assert 'v5-linuxcnc-command-gate' not in state_scope
    for runtime_ini in (
        '/opt/8ax/v5/linuxcnc/ini/v5_bus.ini',
        '/opt/8ax/v5/linuxcnc/ini/v5_pulse.ini',
        '/opt/8ax/v5/linuxcnc/ini/v5_local_shmem.nml',
    ):
        assert runtime_ini in text
    runtime_model_seed = (
        'runtime_seed\tlinuxcnc/ini/v5_bus.ini\t'
        '/opt/8ax/v5/linuxcnc/ini/v5_bus.ini\t0644')
    assert manifest.splitlines().count(runtime_model_seed) == 1
    install_loop_text = section(
        text,
        'while IFS="$tab" read -r kind source destination mode extra; do',
        '\nenable_auxiliary_boot_service() {')
    assert 'if [ "$kind" = "runtime_seed" ] && [ -e "$destination" ]; then' in install_loop_text
    assert 'preserve runtime seed %s -> %s (exists)' in install_loop_text
    for row in (
        'binary\tbuild/board/app/v5_state_publisher\t/usr/libexec/8ax/v5_state_publisher\t0755',
        'init\tservices/state_publisher/init.d/v5-state-publisher\t/etc/init.d/v5-state-publisher\t0755',
    ):
        assert manifest.splitlines().count(row) == 1, 'STATE_WRITER_MANIFEST_EDGE_INVALID:' + row
    stop = section(
        text, 'stop_writer_before_upgrade() {',
        '\nstop_affected_writers_before_install() {')
    dispatch = section(
        text, 'stop_affected_writers_before_install() {',
        '\nstop_shm_abi_domain_before_install() {')
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

    preinstall = text.index('    stop_shm_abi_domain_before_install\n  else')
    install_loop = text.index('while IFS="$tab" read -r kind source destination mode extra; do')
    first_start = text.index('start_shm_abi_domain_after_install() {', install_loop)
    assert preinstall < install_loop < first_start

    # All three current writers and their shared modules select the stop barrier;
    # repeating an install is safe because an absent process is success.
    for token in (
        'manifest_position_publisher=1',
        'manifest_wcs_publisher=1',
        'manifest_state_publisher=1',
        'v5-position-status-publisher',
        'v5-wcs-status-publisher',
        'v5-state-publisher',
        '/usr/libexec/8ax/v5_state_publisher',
        '/usr/libexec/8ax/v5_machine_status_projection.py',
        '/usr/libexec/8ax/v5_wcs_status_codec.py',
    ):
        assert token in text
    assert 'return 1\n  fi\n  for writer_cmdline' in stop

    # Spawn-only publisher services are joined once per non-UI install batch.
    # The UI/cpu-policy/all scopes reuse the UI-owned barrier and must not add
    # a second wait after each individual restart.
    assert text.count('\n    wait_publisher_actual_barrier\n') == 5
    assert text.count('\n  wait_publisher_actual_barrier\n') == 1
    scopes = {
        'actiond': section(
            text, 'elif [ "$restart_scope" = "actiond" ]',
            'elif [ "$restart_scope" = "command_gate" ]'),
        'command_gate': section(
            text, 'elif [ "$restart_scope" = "command_gate" ]',
            'elif [ "$restart_scope" = "backend" ]'),
        'backend': section(text, 'elif [ "$restart_scope" = "backend" ]',
                           'elif [ "$restart_scope" = "ethercat" ]'),
        'ethercat': section(text, 'elif [ "$restart_scope" = "ethercat" ]',
                           'elif [ "$restart_scope" = "wcs" ]'),
        'wcs': section(text, 'elif [ "$restart_scope" = "wcs" ]',
                       'elif [ "$restart_scope" = "cpu_policy" ]'),
        'cpu_policy': section(text, 'elif [ "$restart_scope" = "cpu_policy" ]',
                              'elif [ "$restart_scope" = "settings" ]'),
        'settings': section(text, 'elif [ "$restart_scope" = "settings" ]',
                            '\n  else\n'),
        'all': section(
            text, '\n  else\n    enable_auxiliary_boot_services',
            '\n  fi\n  parameter_table_transaction_complete\nelse\n'),
    }
    for name in ('command_gate', 'backend', 'ethercat', 'wcs', 'settings'):
        assert scopes[name].count('wait_publisher_actual_barrier') == 1, name
    for name in ('cpu_policy', 'all'):
        assert 'wait_publisher_actual_barrier' not in scopes[name], name
    assert scopes['backend'].index('ensure_position_publisher_after_backend') < scopes['backend'].index('wait_publisher_actual_barrier')
    assert scopes['ethercat'].index('ensure_position_publisher_after_backend') < scopes['ethercat'].index('wait_publisher_actual_barrier')
    assert scopes['wcs'].index('/etc/init.d/v5-wcs-status-publisher restart') < scopes['wcs'].index('wait_publisher_actual_barrier')
    assert scopes['settings'].index('/etc/init.d/v5-state-publisher restart') < scopes['settings'].index('wait_publisher_actual_barrier') < scopes['settings'].index('/etc/init.d/v5-settings-actiond restart')
    for name in ('actiond', 'settings', 'all'):
        assert scopes[name].index('parameter_table_transaction_complete') < \
            scopes[name].index('/etc/init.d/v5-settings-actiond restart'), name
    for name in ('command_gate', 'cpu_policy'):
        assert '/etc/init.d/v5-linuxcnc-command-gate restart-native' not in scopes[name]
        assert scopes[name].count(
            '/etc/init.d/v5-linuxcnc-command-gate restart\n') == 1
        assert scopes[name].index('stop_position_publisher_before_backend') < scopes[name].index(
            '/etc/init.d/v5-linuxcnc-command-gate restart') < scopes[name].index(
            'ensure_position_publisher_after_backend')
    for token in ('active_ini=conflict', '--pre-ui-inputs',
                  '--expected-ini "$expected_ini"', '--timeout 120'):
        assert token in actual_barrier
    ensure_position = section(
        text, 'ensure_position_publisher_after_backend() {',
        '\n}\n\nwait_publisher_actual_barrier() {')
    assert '/etc/init.d/v5-position-status-publisher status' in ensure_position
    assert '/etc/init.d/v5-position-status-publisher start' in ensure_position
    assert '/etc/init.d/v5-position-status-publisher restart' not in ensure_position

    # SHM ABI participants are one atomic deployment domain. An incomplete
    # UI-only or State-only manifest is rejected before any installation.
    scanner = section(
        text, 'manifest_ui_only=1',
        '\ncase "$restart_scope_requested" in')
    required_destinations = (
        '/usr/libexec/8ax/v5_lvgl_shell',
        '/usr/libexec/8ax/v5_state_publisher',
        '/usr/libexec/8ax/v5_position_status_publisher',
        '/usr/libexec/8ax/v5_wcs_status_publisher.py',
        '/usr/libexec/8ax/v5_polling_cadence.py',
        '/usr/libexec/8ax/v5_machine_status_projection.py',
        '/usr/libexec/8ax/v5_wcs_status_codec.py',
        '/usr/libexec/8ax/v5_remote_ui_relay.py',
        '/usr/libexec/8ax/v5_ui_boot_ready.py',
        '/usr/libexec/8ax/v5_ui_main_cache_contract.py',
        '/usr/libexec/8ax/v5_status_shm_reader.py',
        '/etc/init.d/v5-position-status-publisher',
        '/etc/init.d/v5-wcs-status-publisher',
        '/etc/init.d/v5-state-publisher',
        '/etc/init.d/v5-ui-relay',
    )
    manifest_rows = [
        line for line in manifest.splitlines()
        if line and not line.startswith('#') and
        line.split('\t')[2] in required_destinations
    ]
    assert len(manifest_rows) == len(required_destinations)
    assert {line.split('\t')[2] for line in manifest_rows} == set(required_destinations)

    def run_manifest_scan(
            rows, linuxcnc_bundle_enabled: int = 0) -> subprocess.CompletedProcess:
        with tempfile.TemporaryDirectory() as directory:
            manifest_path = Path(directory) / 'manifest.tsv'
            manifest_path.write_bytes(('\n'.join(rows) + '\n').encode('utf-8'))
            script = (
                'tab=$(printf "\\t")\n'
                'uname() { printf "%s\\n" "5.4.0-rt7-rt1-xilinx-v2020.2"; }\n'
                f'manifest="{manifest_path.as_posix()}"\n'
                f'linuxcnc_bundle_enabled={linuxcnc_bundle_enabled}\n'
                f'{scanner}\n'
                'echo "ABI_COMPLETE=$manifest_shm_abi_complete '
                'ABI_TOUCHED=$manifest_shm_abi_touched '
                'EC_COMPLETE=$manifest_ethercat_complete '
                'EC_TOUCHED=$manifest_ethercat_touched"\n'
            )
            script_path = Path(directory) / 'manifest-scan.sh'
            script_path.write_text(script, encoding='utf-8')
            return subprocess.run(
                ['sh', script_path.as_posix()], check=False,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    complete = run_manifest_scan(manifest_rows)
    assert complete.returncode == 0, complete.stderr
    assert b'ABI_COMPLETE=1 ABI_TOUCHED=1' in complete.stdout
    for destination in (
        '/usr/libexec/8ax/v5_lvgl_shell',
        '/usr/libexec/8ax/v5_state_publisher',
        '/usr/libexec/8ax/v5_position_status_publisher',
        '/usr/libexec/8ax/v5_status_shm_reader.py',
    ):
        incomplete_rows = [
            row for row in manifest_rows if row.split('\t')[2] != destination]
        incomplete = run_manifest_scan(incomplete_rows)
        assert incomplete.returncode == 8, (destination, incomplete.stderr)
        assert b'complete Position/State/UI atomic bundle' in incomplete.stderr

    ethercat_destinations = (
        '/lib/modules/5.4.0-rt7-rt1-xilinx-v2020.2/ethercat/master/ec_master.ko',
        '/lib/modules/5.4.0-rt7-rt1-xilinx-v2020.2/ethercat/devices/ec_generic.ko',
        '/usr/lib/linuxcnc/modules/lcec.so',
        '/usr/libexec/8ax/v5_ethercat_backend_lifecycle.sh',
    )
    ethercat_rows = [
        line for line in manifest.splitlines()
        if line and not line.startswith('#') and
        line.split('\t')[2] in ethercat_destinations
    ]
    assert len(ethercat_rows) == 4
    ethercat_complete = run_manifest_scan(ethercat_rows)
    assert ethercat_complete.returncode == 0, ethercat_complete.stderr
    assert b'EC_COMPLETE=1 EC_TOUCHED=1' in ethercat_complete.stdout
    mixed_complete = run_manifest_scan(
        ethercat_rows + ['gcode\tgcode/golden/cc-ac.ngc\t'
                         '/opt/8ax/v5/gcode/golden/cc-ac.ngc\t0644'])
    assert mixed_complete.returncode == 0, mixed_complete.stderr
    assert b'EC_COMPLETE=1 EC_TOUCHED=1' in mixed_complete.stdout
    for destination in ethercat_destinations:
        incomplete_rows = [
            row for row in ethercat_rows if row.split('\t')[2] != destination]
        incomplete = run_manifest_scan(incomplete_rows)
        assert incomplete.returncode == 8, (destination, incomplete.stderr)
        assert b'complete ec_master/ec_generic/lcec/lifecycle atomic bundle' in incomplete.stderr

    # The versioned native owner protocol is one atomic deployment domain:
    # C server, Python zero client, and LinuxCNC owner/router move together.
    command_gate_row = (
        'binary\tbuild/board/app/v5_command_gate_server\t'
        '/usr/libexec/8ax/v5_command_gate_server\t0755')
    zero_client_row = (
        'module\tservices/drive_profile/v5_command_gate_zero_client.py\t'
        '/usr/libexec/8ax/drive_profile/v5_command_gate_zero_client.py\t0644')
    assert manifest.splitlines().count(command_gate_row) == 1
    assert manifest.splitlines().count(zero_client_row) == 1
    gate_without_owner = run_manifest_scan([command_gate_row])
    assert gate_without_owner.returncode == 8, gate_without_owner.stderr
    assert b'as one atomic bundle' in gate_without_owner.stderr
    zero_without_owner = run_manifest_scan([zero_client_row])
    assert zero_without_owner.returncode == 8, zero_without_owner.stderr
    assert b'as one atomic bundle' in zero_without_owner.stderr
    owner_without_gate = run_manifest_scan([], linuxcnc_bundle_enabled=1)
    assert owner_without_gate.returncode == 8, owner_without_gate.stderr
    assert b'as one atomic bundle' in owner_without_gate.stderr
    atomic_native_protocol = run_manifest_scan(
        [command_gate_row, zero_client_row], linuxcnc_bundle_enabled=1)
    assert atomic_native_protocol.returncode == 0, atomic_native_protocol.stderr
    unregistered_gate = run_manifest_scan([
        command_gate_row.replace(
            'build/board/app/v5_command_gate_server',
            'build/board/app/stale_v5_command_gate_server')
    ], linuxcnc_bundle_enabled=1)
    assert unregistered_gate.returncode == 6, unregistered_gate.stderr
    assert b'registered ARM artifact and mode 0755' in unregistered_gate.stderr
    unregistered_zero_client = run_manifest_scan([
        command_gate_row,
        zero_client_row.replace(
            'services/drive_profile/v5_command_gate_zero_client.py',
            'services/drive_profile/stale_zero_client.py'),
    ], linuxcnc_bundle_enabled=1)
    assert unregistered_zero_client.returncode == 6, unregistered_zero_client.stderr
    assert b'registered source and mode 0644' in unregistered_zero_client.stderr
    linuxcnc_bundle_verifier = section(
        text, 'verify_linuxcnc_deploy_bundle() {',
        '\ninstall_linuxcnc_deploy_bundle() {')
    for required_owner in (
        '$linuxcnc_package_root/usr/bin/v5_native_hal_owner',
        '$linuxcnc_package_root/usr/lib/linuxcnc/modules/v5_bus_axis_router.so',
        'LinuxCNC deploy bundle is missing native protocol owner',
    ):
        assert required_owner in linuxcnc_bundle_verifier, required_owner

    ethercat_stop = text.index(
        'if [ "$apply" -eq 1 ] && [ "$manifest_ethercat_complete" -eq 1 ]; then\n'
        '  stop_ethercat_modules_before_install')
    ethercat_stop_function = section(
        text, 'stop_ethercat_modules_before_install() {',
        '\nensure_position_publisher_after_backend() {')
    assert 'if ! /etc/init.d/v5-linuxcnc-command-gate stop; then' in ethercat_stop_function
    assert 'continuing verified idempotent teardown' in ethercat_stop_function
    assert 'for process in rtapi_app linuxcncsvr milltask io linuxcncrsh v5_command_gate_server' in ethercat_stop_function
    assert ethercat_stop_function.count(
        "grep -Eq '^(ec_master|ec_generic)[[:space:]]' /proc/modules") == 2
    install_loop_index = text.index(
        'while IFS="$tab" read -r kind source destination mode extra; do',
        ethercat_stop)
    actiond_stop_index = text.index(
        'if [ "$manifest_actiond_touched" -eq 1 ]; then\n'
        '    stop_settings_actiond_before_install')
    assert actiond_stop_index < install_loop_index
    depmod_index = text.index(
        'if [ "$apply" -eq 1 ] && [ "$manifest_ethercat_complete" -eq 1 ]; then\n'
        '  [ -x /sbin/depmod ] || {', install_loop_index)
    assert ethercat_stop < install_loop_index < depmod_index
    assert '/sbin/depmod -a' in text[depmod_index:depmod_index + 300]
    assert '\n  depmod -a\n' not in text

    atomic_stop = section(
        text, 'stop_shm_abi_domain_before_install() {',
        '\nwait_position_shm_abi_readback() {')
    atomic_start = section(
        text, 'start_shm_abi_domain_after_install() {',
        '\nretired_pid_matches_path() {')
    assert atomic_stop.index('/etc/init.d/v5-ui-relay stop') < atomic_stop.index(
        'v5-state-publisher') < atomic_stop.index(
        'v5-wcs-status-publisher') < atomic_stop.index(
        'v5-position-status-publisher')
    assert atomic_start.index('/etc/init.d/v5-position-status-publisher start') < atomic_start.index(
        'wait_position_shm_abi_readback') < atomic_start.index(
        '/etc/init.d/v5-state-publisher start') < atomic_start.index(
        'wait_state_shm_abi_readback') < atomic_start.index(
        '/etc/init.d/v5-ui-relay start')
    for token in (
        'V5_POSITION_ABI_READBACK_OK',
        '(0x56504F53, 3, 256)',
        'writer_identity == 0',
        'fnv1a(payload[:248])',
        'V5_STATE_ABI_READBACK_OK',
        '0x56355348, 3, 7128, 7128, 7096',
        'required_mask = (1 << 0) | (1 << 1) | (1 << 8)',
        'scene_generation == 0',
        'zlib.crc32(payload[32:], actual_crc)',
        'V5_SHM_ABI_ATOMIC_RESTART_OK scope=position,state,ui-relay',
    ):
        assert token in text, token

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
PUBLISHER_SNAPSHOT_PATH="{(root / 'ui_input_barrier.json').as_posix()}"
{actual_barrier}
wait_publisher_actual_barrier
'''
            return subprocess.run(['sh', '-c', script], check=False,
                                  stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE)

        assert run_barrier().returncode == 0
        assert barrier_log.read_text(encoding='utf-8').strip() == (
            '--pre-ui-inputs --expected-ini '
            '/opt/8ax/v5/linuxcnc/ini/v5_bus.ini '
            '--publisher-snapshot-path ' +
            (root / 'ui_input_barrier.json').as_posix() + ' --timeout 120')
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
        writer = '/usr/libexec/8ax/v5_state_publisher'
        legacy_pidfile = Path(directory) / 'v5_state_publisher.pid'

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

        # Active orphan: no PIDFILE exists, but the exact argv owner is stopped.
        cmdline.write_bytes(writer.encode() + b'\0--path\0/dev/shm/v3_status_shm\0')
        assert run('stop').returncode == 0
        assert not cmdline.exists()

        # A legacy one-field diagnostic record cannot hide the same orphan.
        legacy_pidfile.write_text('123\n', encoding='ascii')
        cmdline.write_bytes(writer.encode() + b'\0--path\0/dev/shm/v3_status_shm\0')
        assert run('stop').returncode == 0
        assert not cmdline.exists()

        # PID reuse text does not match the canonical writer and is untouched.
        cmdline.write_bytes(b'/usr/libexec/8ax/other_state_publisher\0')
        assert run('stop').returncode == 0
        assert cmdline.exists()

        # Matching writer that ignores TERM makes the pre-install barrier fail.
        cmdline.write_bytes(writer.encode() + b'\0--path\0/dev/shm/v3_status_shm\0')
        assert run('timeout').returncode != 0

        # Retry after the old process is gone is idempotent.
        cmdline.unlink()
        assert run('stop').returncode == 0

    # The same detector must fail if any State selector/dispatch edge is
    # removed; broad process killers and KILL fallback remain forbidden.
    for token in STATE_SCANNER_EDGES + STATE_DISPATCH_EDGES:
        start_token, end_token = (
            ('manifest_position_publisher=0', '\ncase "$restart_scope_requested" in')
            if token in STATE_SCANNER_EDGES else
            ('stop_affected_writers_before_install() {',
             '\nstop_shm_abi_domain_before_install() {'))
        begin = text.index(start_token); end = text.index(end_token, begin)
        body = text[begin:end].replace(token, 'STATE_UPGRADE_EDGE_REMOVED', 1)
        mutated = text[:begin] + body + text[end:]
        try:
            audit_state_upgrade(mutated)
        except AssertionError as exc:
            assert 'STATE_WRITER_' in str(exc)
        else:
            raise AssertionError('STATE_WRITER_UPGRADE_MUTATION_SURVIVED:' + token)
    assert 'pkill' not in functions and 'killall' not in functions

    # Retired init scripts are never executed; only exact process cleanup and
    # one-way rm-f tombstones remain.
    assert '"$retired_init" stop' not in text
    assert 'rm -f /etc/init.d/v5-rtcp-status-publisher' in text
    assert 'rm -f /etc/init.d/v5-g53-geometry-memory-owner' in text
    assert 'rm -f /usr/libexec/8ax/drive_profile/v5_bus_zero_resident_gate.py' in text

    print('V5_RUNTIME_WRITER_UPGRADE_ORDER_OK')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
