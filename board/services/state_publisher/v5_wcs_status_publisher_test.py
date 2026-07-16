#!/usr/bin/env python3
from __future__ import annotations

import tempfile
from pathlib import Path

import v5_wcs_status_codec as codec
import v5_machine_status_projection as projection
import v5_wcs_status_publisher as publisher


class FakeStat:
    def __init__(self):
        self.g5x_index = 1
        self.g5x_offset = [0.0] * 9
        self.gcodes = ()
        self.joint_actual_position = [0.0] * 5
        self.joint_position = [0.0] * 5
        self.position = [999.0] * 5
        self.spindle = ({"speed": 0.0, "override": 1.0},)
        self.feedrate = 1.0
        self.current_vel = 0.0
        self.poll_count = 0

    def poll(self):
        self.poll_count += 1

    @property
    def joint(self):
        raise AssertionError('retired heavy joint dictionary path was accessed')


class FakeHalComponent:
    def __init__(self):
        self.is_ready = False
        self.values = {}

    def newpin(self, name, _pin_type, _direction):
        assert not self.is_ready
        self.values[name] = 0

    def ready(self):
        self.is_ready = True

    def __getitem__(self, name):
        assert self.is_ready
        return self.values[name]


class FakeHalModule:
    HAL_BIT = 1
    HAL_U32 = 2
    HAL_FLOAT = 3
    HAL_IN = 4

    def __init__(self):
        self.created_name = None
        self.component_instance = FakeHalComponent()
        self.connections = {}

    def component(self, name):
        self.created_name = name
        return self.component_instance

    def connect(self, pin_name, signal_name):
        assert not self.component_instance.is_ready
        self.connections[pin_name] = signal_name


def check_hal_read_access_registers_ready_component() -> None:
    module = FakeHalModule()
    access = publisher.HalReadAccess(module)
    assert module.created_name == publisher.HAL_DISPLAY_PROJECTION_COMPONENT
    owner_name = 'v5-native-hal-owner.wcp-a-valid'
    local_name = access._pin_names[owner_name]
    module.component_instance.values[local_name] = True
    assert access.get_value(owner_name) is True
    assert module.connections[
        f'{publisher.HAL_DISPLAY_PROJECTION_COMPONENT}.{local_name}'] == 'v5-wcheckpoint-a-valid'
    assert max(map(len, module.connections)) <= 47


def check_resident_epoch_changes_only_with_table() -> None:
    owner = publisher.ResidentWcsParameterOwner()
    owner.table = publisher.empty_wcs_table()
    owner.epoch = 17
    assert owner.update_active_from_stat(0, [0.0] * publisher.WCS_AXIS_COUNT)
    assert owner.epoch == 17
    assert owner.update_active_from_stat(0, [1.0, 0.0, 0.0, 0.0, 0.0])
    assert owner.epoch == 18
    assert owner.update_active_from_stat(0, [1.0, 0.0, 0.0, 0.0, 0.0])
    assert owner.epoch == 18


def check_poll_cadence() -> None:
    counts = {"wcs": 0, "position_samples": 0, "modal": 0, "modal_compute": 0}
    publisher.write_status = lambda *args, **kwargs: counts.__setitem__("wcs", counts["wcs"] + 1)
    publisher.write_position_status = lambda *args, **kwargs: counts.__setitem__("position_samples", counts["position_samples"] + 1)
    publisher.write_modal_tool_status = lambda *args, **kwargs: counts.__setitem__("modal", counts["modal"] + 1)
    def modal_text(_gcodes, g5x):
        counts["modal_compute"] += 1
        return f"G{g5x}"
    publisher.modal_text_from_gcodes = modal_text
    publisher.current_tool_number = lambda _stat: 0
    publisher.current_tool_length = lambda *_args: (1, 15.0)
    publisher.interpreter_idle_from_stat = lambda *_args: (1, 1)
    publisher.interpreter_paused_from_stat = lambda *_args: (1, 0)
    publisher.all_homed_from_stat = lambda _stat: (1, 1)
    publisher.line_from_stat = lambda _stat, _name: (1, 0)
    publisher.mdi_run_from_stat = lambda _stat, _line: (1, 0, 0, "")

    stat = FakeStat()
    owner = publisher.ResidentWcsParameterOwner()
    owner.table = publisher.empty_wcs_table()
    owner.epoch = 23
    cadence = publisher.StatusPublishCadence(0.2)
    for index in range(31):
        publisher.poll_once(
            stat, owner, "/dev/null/wcs", "/dev/null/position", "/dev/null/modal",
            publish_cadence=cadence, now_monotonic=index * 0.033)
    assert stat.poll_count == 31
    assert counts["position_samples"] == 31
    assert 5 <= counts["wcs"] <= 6
    assert 5 <= counts["modal"] <= 6
    assert counts["modal_compute"] == counts["modal"]
    epoch_before_active_change = owner.epoch
    wcs_before_active_change = counts["wcs"]
    stat.g5x_index = 2
    publisher.poll_once(
        stat, owner, "/dev/null/wcs", "/dev/null/position", "/dev/null/modal",
        publish_cadence=cadence, now_monotonic=1.024)
    assert counts["wcs"] == wcs_before_active_change + 1
    assert owner.epoch == epoch_before_active_change


def check_position_change_or_heartbeat_publish() -> None:
    stat = FakeStat()
    cadence = publisher.StatusPublishCadence(0.2)
    write_times = []
    now = [0.0]
    original_atomic_write = projection.atomic_write
    projection.atomic_write = lambda _path, _payload: write_times.append(now[0])
    try:
        for index in range(31):
            now[0] = index * 0.033
            projection.write_position_status('/dev/null/position', stat, publish_cadence=cadence, now_monotonic=now[0])
        assert 7 <= len(write_times) <= 9
        assert max(right - left for left, right in zip(write_times, write_times[1:])) <= 0.133
        previous_count = len(write_times)
        stat.joint_actual_position[0] = 1.0e-12
        now[0] = 1.024
        projection.write_position_status('/dev/null/position', stat, publish_cadence=cadence, now_monotonic=now[0])
        assert len(write_times) == previous_count + 1

        moving_cadence = publisher.StatusPublishCadence(0.2)
        moving_writes = []
        projection.atomic_write = lambda _path, _payload: moving_writes.append(1)
        for index in range(10):
            stat.joint_actual_position[0] = float(index)
            projection.write_position_status(
                '/dev/null/position', stat, publish_cadence=moving_cadence, now_monotonic=index * 0.033)
        assert len(moving_writes) == 10

        retry_cadence = publisher.StatusPublishCadence(0.2)
        attempts = []
        def fail_once(_path, _payload):
            attempts.append(1)
            if len(attempts) == 1:
                raise OSError('expected write failure')
        projection.atomic_write = fail_once
        try:
            projection.write_position_status(
                '/dev/null/position', stat, publish_cadence=retry_cadence, now_monotonic=0.0)
        except OSError:
            pass
        else:
            raise AssertionError('position write failure was swallowed')
        projection.write_position_status(
            '/dev/null/position', stat, publish_cadence=retry_cadence, now_monotonic=0.033)
        assert len(attempts) == 2
    finally:
        projection.atomic_write = original_atomic_write


def check_position_uses_continuous_joint_position() -> None:
    stat = FakeStat()
    stat.joint_position[4] = -19079.946
    captured = {}

    class CaptureProjection:
        def project(self, raw_mcs, raw_cmd):
            captured['raw_cmd'] = list(raw_cmd)
            return raw_mcs, raw_cmd

    original_atomic_write = projection.atomic_write
    projection.atomic_write = lambda _path, _payload: None
    try:
        projection.write_position_status(
            '/dev/null/position', stat, rotary_projection=CaptureProjection())
    finally:
        projection.atomic_write = original_atomic_write
    assert captured['raw_cmd'][4] == -19079.946
    assert captured['raw_cmd'][4] != stat.position[4]


def check_atomic_shm_write_needs_no_fsync() -> None:
    with tempfile.TemporaryDirectory(prefix="v5_wcs_codec_") as temporary:
        path = Path(temporary) / "status.bin"
        original_fsync = getattr(codec.os, "fsync", None)
        codec.os.fsync = lambda _fd: (_ for _ in ()).throw(AssertionError("fsync called"))
        try:
            codec.atomic_write(str(path), b"fresh-native-status")
        finally:
            if original_fsync is not None:
                codec.os.fsync = original_fsync
        assert path.read_bytes() == b"fresh-native-status"


def main() -> int:
    check_hal_read_access_registers_ready_component()
    check_resident_epoch_changes_only_with_table()
    check_poll_cadence()
    check_position_change_or_heartbeat_publish()
    check_position_uses_continuous_joint_position()
    check_atomic_shm_write_needs_no_fsync()
    print("V5_WCS_STATUS_PUBLISHER_TEST_OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
