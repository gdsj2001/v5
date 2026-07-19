#!/usr/bin/env python3
from __future__ import annotations

import math

from v5_machine_status_projection import (
    NativeRotaryDisplayProjection,
    display_position_projection,
)


class FakeHal:
    def __init__(self, values):
        self.values = dict(values)
        self.read_counts = {}

    def get_value(self, name):
        self.read_counts[name] = self.read_counts.get(name, 0) + 1
        if name not in self.values:
            raise AssertionError(f'unexpected HAL read: {name}')
        return self.values[name]

    def read_count(self, name):
        return self.read_counts.get(name, 0)


class SequencedHal(FakeHal):
    def __init__(self, values, sequences):
        super().__init__(values)
        self.sequences = {name: list(items) for name, items in sequences.items()}

    def get_value(self, name):
        self.read_counts[name] = self.read_counts.get(name, 0) + 1
        sequence = self.sequences.get(name)
        if sequence:
            return sequence.pop(0)
        if name not in self.values:
            raise AssertionError(f'unexpected HAL read: {name}')
        return self.values[name]


def projection_values():
    values = {
        'v5-native-hal-owner.display-metadata-valid': True,
        'v5-native-hal-owner.display-metadata-generation': 0x62E4A301,
        'v5-native-hal-owner.display-active-mask': 0x1F,
        'v5-native-hal-owner.display-commit-seq': 1,
        'v5-native-hal-owner.home-table-mapping-valid': True,
        'v5-native-hal-owner.home-table-map-gen': 0x46A18ECC,
        'v5-native-hal-owner.home-table-active-mask': 0x1F,
        'v5-native-hal-owner.home-table-commit-seq': 1,
    }
    axes = ((0, 'X', 0, 26, 10000), (1, 'Y', 1, 4, 10000),
            (2, 'Z', 2, 36, 10000), (3, 'A', 3, 5, 10000),
            (4, 'C', 4, 24, 10000))
    for joint, axis, slot, zero, scale in axes:
        suffix = f'{joint:02d}'
        values[f'v5-native-hal-owner.display-axis-code-{suffix}'] = ord(axis)
        values[
            f'v5-native-hal-owner.display-unit-per-count-{suffix}'
        ] = 1.0 / float(scale)
        values[f'v5-native-hal-owner.home-config-valid-{suffix}'] = True
        values[f'v5-native-hal-owner.home-mapping-generation-{suffix}'] = 0x46A18ECC
        values[f'v5-native-hal-owner.home-axis-code-{suffix}'] = ord(axis)
        values[f'v5-native-hal-owner.home-status-slot-{suffix}'] = slot
        values[f'v5-native-hal-owner.home-zero-counts-{suffix}'] = float(zero)
        values[f'v5-native-hal-owner.home-counts-per-unit-{suffix}'] = float(scale)
    values.update({
        'v5-native-hal-owner.wcp-a-valid': True,
        'v5-native-hal-owner.wcp-a-generation': 7,
        'v5-native-hal-owner.wcp-a-logical-counts': 0.0,
        'v5-native-hal-owner.wcp-a-base-counts': 0.0,
        'v5-native-hal-owner.wcp-a-runtime-counts': 0.0,
        'v5-native-hal-owner.wcp-c-valid': True,
        'v5-native-hal-owner.wcp-c-generation': 9,
        'v5-native-hal-owner.wcp-c-logical-counts': -18000000.0,
        'v5-native-hal-owner.wcp-c-base-counts': 0.0,
        'v5-native-hal-owner.wcp-c-runtime-counts': -18000000.0,
    })
    return values


def check_router_zero_relative_counts_are_not_offset_twice() -> None:
    values = projection_values()
    projection = NativeRotaryDisplayProjection(FakeHal(values))
    raw_mcs = [0.0, 0.0, 0.0, 0.0, -18000.0]
    raw_cmd = list(raw_mcs)
    mcs, cmd = projection.project(raw_mcs, raw_cmd)
    assert mcs[3] == 0.0 and mcs[4] == 0.0
    assert cmd[3] == 0.0 and cmd[4] == 0.0
    assert raw_mcs[3] == 0.0 and raw_mcs[4] == -18000.0


def check_display_projection_rounds_fourth_digit_for_ui_only() -> None:
    low = display_position_projection([0.0, 29.9824, 0.0, 0.0, 0.0])
    high = display_position_projection([0.0, 29.9825, 0.0, 0.0, 0.0])
    edge = display_position_projection([0.0, 29.9830, 0.0, 0.0, 0.0])
    negative = display_position_projection([0.0, -29.9829, 0.0, 0.0, 0.0])
    negative_zero = display_position_projection([0.0, -0.0001, 0.0, 0.0, 0.0])
    rotary_wrap = display_position_projection([0.0, 359.9999, 0.0, 0.0, 0.0])
    assert low[1] == 29.982
    assert high[1] == 29.983
    assert edge[1] == 29.983
    assert negative[1] == -29.983
    assert negative_zero[1] == 0.0
    assert rotary_wrap[1] == 360.0


def check_one_count_displays_one_count_not_clamped() -> None:
    values = projection_values()
    values['v5-native-hal-owner.wcp-a-logical-counts'] = 1.0
    values['v5-native-hal-owner.wcp-a-runtime-counts'] = 1.0
    projection = NativeRotaryDisplayProjection(FakeHal(values))
    mcs, _ = projection.project(
        [0.0, 0.0, 0.0, 0.0001, -18000.0],
        [0.0, 0.0, 0.0, 0.0001, -18000.0])
    assert math.isclose(mcs[3], 0.0001, abs_tol=1.0e-12)


def check_fractional_actual_count_is_preserved() -> None:
    values = projection_values()
    values['v5-native-hal-owner.wcp-a-logical-counts'] = 251835.5
    values['v5-native-hal-owner.wcp-a-runtime-counts'] = 251835.5
    projection = NativeRotaryDisplayProjection(FakeHal(values))
    mcs, _ = projection.project(
        [0.0, 0.0, 0.0, 25.18355, -18000.0], None)
    assert math.isclose(mcs[3], 25.18355, abs_tol=1.0e-12)


def check_fractional_command_phase_is_not_truncated() -> None:
    values = projection_values()
    projection = NativeRotaryDisplayProjection(FakeHal(values))
    _, cmd = projection.project(
        [0.0, 0.0, 0.0, 0.0, -18000.0],
        [0.0, 0.0, 0.0, 0.0005, -18000.0])
    assert math.isclose(cmd[3], 0.0005, abs_tol=1.0e-12)


def check_actual_projects_without_command_input() -> None:
    projection = NativeRotaryDisplayProjection(FakeHal(projection_values()))
    mcs, cmd = projection.project(
        [0.0, 0.0, 0.0, 0.0, -18000.0], None)
    assert mcs[3] == 0.0 and mcs[4] == 0.0
    assert cmd is None


def check_bc_mapping_projects_b_and_c() -> None:
    values = projection_values()
    values['v5-native-hal-owner.display-axis-code-03'] = ord('B')
    values['v5-native-hal-owner.home-axis-code-03'] = ord('B')
    for field in ('valid', 'generation', 'logical-counts', 'base-counts', 'runtime-counts'):
        values[f'v5-native-hal-owner.wcp-b-{field}'] = values.pop(
            f'v5-native-hal-owner.wcp-a-{field}')
    projection = NativeRotaryDisplayProjection(FakeHal(values))
    mcs, _ = projection.project(
        [0.0, 0.0, 0.0, 0.0, -18000.0], None)
    assert mcs[3] == 0.0 and mcs[4] == 0.0


def check_display_metadata_comes_from_active_model_scale() -> None:
    values = projection_values()
    values['v5-native-hal-owner.display-unit-per-count-01'] = 0.0005
    values['v5-native-hal-owner.home-counts-per-unit-01'] = 2000.0
    projection = NativeRotaryDisplayProjection(FakeHal(values))
    unit_per_count, display_digits = projection.display_metadata()
    assert unit_per_count == [0.0001, 0.0005, 0.0001, 0.0001, 0.0001]
    assert display_digits == [3, 3, 3, 3, 3]


def check_invalid_native_mapping_fails_closed() -> None:
    values = projection_values()
    values['v5-native-hal-owner.home-table-mapping-valid'] = False
    projection = NativeRotaryDisplayProjection(FakeHal(values))
    try:
        projection.project([0.0] * 5, [0.0] * 5)
    except RuntimeError as exc:
        assert str(exc) == 'native_rotary_projection_mapping_invalid'
    else:
        raise AssertionError('invalid native mapping did not fail closed')


def check_unconfigured_zero_table_keeps_native_display_available() -> None:
    values = projection_values()
    values.update({
        'v5-native-hal-owner.home-table-mapping-valid': False,
        'v5-native-hal-owner.home-table-map-gen': 0,
        'v5-native-hal-owner.home-table-active-mask': 0,
        'v5-native-hal-owner.home-table-commit-seq': 0,
    })
    hal = FakeHal(values)
    projection = NativeRotaryDisplayProjection(hal)
    raw_mcs = [1.0, 2.0, 3.0, -0.0001, 360.0001]
    raw_cmd = [4.0, 5.0, 6.0, 720.0001, -360.0001]
    mcs, cmd = projection.project(raw_mcs, raw_cmd)
    assert mcs[:3] == raw_mcs[:3] and cmd[:3] == raw_cmd[:3]
    assert math.isclose(mcs[3], 359.9999, abs_tol=1.0e-12)
    assert math.isclose(mcs[4], 0.0001, abs_tol=1.0e-12)
    assert math.isclose(cmd[3], 0.0001, abs_tol=1.0e-12)
    assert math.isclose(cmd[4], 359.9999, abs_tol=1.0e-12)
    assert raw_mcs[3:] == [-0.0001, 360.0001]
    unit_per_count, display_digits = projection.display_metadata()
    assert unit_per_count == [0.0001] * 5
    assert display_digits == [3] * 5
    assert not any(name.startswith('v5-native-hal-owner.wcp-')
                   for name in hal.read_counts)


def check_inconsistent_wcheckpoint_window_fails_closed() -> None:
    values = projection_values()
    values['v5-native-hal-owner.wcp-c-runtime-counts'] += 1.0
    projection = NativeRotaryDisplayProjection(FakeHal(values))
    try:
        projection.project([0.0] * 5, [0.0] * 5)
    except RuntimeError as exc:
        assert str(exc) == 'native_rotary_projection_readback_invalid'
    else:
        raise AssertionError('inconsistent native wcheckpoint window did not fail closed')


def check_wcheckpoint_valid_drop_fails_closed() -> None:
    values = projection_values()
    projection = NativeRotaryDisplayProjection(SequencedHal(values, {
        'v5-native-hal-owner.wcp-a-valid':
            [True, False, True, False, True, False],
    }))
    try:
        projection.project([0.0] * 5, None)
    except RuntimeError as exc:
        assert str(exc) == 'native_rotary_projection_readback_invalid'
    else:
        raise AssertionError('native wcheckpoint valid drop did not fail closed')


def check_stable_generation_uses_fast_metadata_path() -> None:
    hal = FakeHal(projection_values())
    projection = NativeRotaryDisplayProjection(hal)
    for _sample in range(31):
        projection.project(
            [0.0, 0.0, 0.0, 0.0, -18000.0],
            [0.0, 0.0, 0.0, 0.0, -18000.0])

    # Static mapping and wcheckpoint metadata are loaded once.  Every 33 ms
    # sample still reads fresh validity, generation and logical counts.
    assert hal.read_count(
        'v5-native-hal-owner.display-unit-per-count-03') == 1
    assert hal.read_count(
        'v5-native-hal-owner.display-unit-per-count-04') == 1
    assert hal.read_count('v5-native-hal-owner.home-table-active-mask') == 2
    assert hal.read_count('v5-native-hal-owner.home-table-commit-seq') == 32
    assert hal.read_count('v5-native-hal-owner.home-counts-per-unit-03') == 1
    assert hal.read_count('v5-native-hal-owner.home-counts-per-unit-04') == 1
    for axis in ('a', 'c'):
        assert hal.read_count(f'v5-native-hal-owner.wcp-{axis}-base-counts') == 1
        assert hal.read_count(f'v5-native-hal-owner.wcp-{axis}-runtime-counts') == 1
        assert hal.read_count(f'v5-native-hal-owner.wcp-{axis}-logical-counts') == 31
        assert hal.read_count(f'v5-native-hal-owner.wcp-{axis}-valid') == 62
        assert hal.read_count(f'v5-native-hal-owner.wcp-{axis}-generation') == 62


def check_generation_change_reloads_all_metadata_immediately() -> None:
    values = projection_values()
    hal = FakeHal(values)
    projection = NativeRotaryDisplayProjection(hal)
    projection.project([0.0] * 5, [0.0] * 5)

    new_generation = values['v5-native-hal-owner.home-table-map-gen'] + 1
    hal.values['v5-native-hal-owner.home-table-map-gen'] = new_generation
    for joint in range(5):
        hal.values[
            f'v5-native-hal-owner.home-mapping-generation-{joint:02d}'
        ] = new_generation
    projection.project([0.0] * 5, [0.0] * 5)

    assert hal.read_count('v5-native-hal-owner.home-counts-per-unit-03') == 2
    assert hal.read_count('v5-native-hal-owner.home-counts-per-unit-04') == 2
    assert hal.read_count('v5-native-hal-owner.wcp-a-base-counts') == 2
    assert hal.read_count('v5-native-hal-owner.wcp-c-base-counts') == 2

    # A parameter-only controlled reload advances commit_seq even when the
    # axis/slave generation hash is unchanged.  It must still invalidate all
    # cached mapping and wcheckpoint metadata on the very next sample.
    hal.values['v5-native-hal-owner.home-table-commit-seq'] += 1
    projection.project([0.0] * 5, [0.0] * 5)
    assert hal.read_count('v5-native-hal-owner.home-counts-per-unit-03') == 3
    assert hal.read_count('v5-native-hal-owner.home-counts-per-unit-04') == 3
    assert hal.read_count('v5-native-hal-owner.wcp-a-base-counts') == 3
    assert hal.read_count('v5-native-hal-owner.wcp-c-base-counts') == 3

    hal.values['v5-native-hal-owner.wcp-a-generation'] += 1
    hal.values['v5-native-hal-owner.wcp-a-logical-counts'] = 10.0
    hal.values['v5-native-hal-owner.wcp-a-base-counts'] = 4.0
    hal.values['v5-native-hal-owner.wcp-a-runtime-counts'] = 6.0
    _, cmd = projection.project(
        [0.0] * 5, [0.0, 0.0, 0.0, 0.006, 0.0])
    assert hal.read_count('v5-native-hal-owner.wcp-a-base-counts') == 4
    assert math.isclose(cmd[3], 0.0064, abs_tol=1.0e-12)


def check_new_projection_instance_rebuilds_resident_cache() -> None:
    hal = FakeHal(projection_values())
    first = NativeRotaryDisplayProjection(hal)
    first.project([0.0] * 5, [0.0] * 5)
    first.project([0.0] * 5, [0.0] * 5)
    assert hal.read_count('v5-native-hal-owner.home-counts-per-unit-03') == 1
    assert hal.read_count('v5-native-hal-owner.wcp-a-base-counts') == 1

    restarted = NativeRotaryDisplayProjection(hal)
    restarted.project([0.0] * 5, [0.0] * 5)
    assert hal.read_count('v5-native-hal-owner.home-counts-per-unit-03') == 2
    assert hal.read_count('v5-native-hal-owner.wcp-a-base-counts') == 2


def check_invalid_sample_drops_cache_before_same_generation_recovers() -> None:
    values = projection_values()
    hal = FakeHal(values)
    projection = NativeRotaryDisplayProjection(hal)
    projection.project([0.0] * 5, [0.0] * 5)

    hal.values['v5-native-hal-owner.wcp-a-valid'] = False
    try:
        projection.project([0.0] * 5, [0.0] * 5)
    except RuntimeError as exc:
        assert str(exc) == 'native_rotary_projection_readback_invalid'
    else:
        raise AssertionError('invalid wcheckpoint sample reused cached metadata')

    hal.values['v5-native-hal-owner.wcp-a-valid'] = True
    hal.values['v5-native-hal-owner.wcp-a-logical-counts'] = 12.0
    hal.values['v5-native-hal-owner.wcp-a-base-counts'] = 5.0
    hal.values['v5-native-hal-owner.wcp-a-runtime-counts'] = 7.0
    _, cmd = projection.project(
        [0.0] * 5, [0.0, 0.0, 0.0, 0.003, 0.0])
    assert hal.read_count('v5-native-hal-owner.wcp-a-base-counts') == 2
    assert math.isclose(cmd[3], 0.0035, abs_tol=1.0e-12)

    hal.values['v5-native-hal-owner.home-table-mapping-valid'] = False
    try:
        projection.project([0.0] * 5, [0.0] * 5)
    except RuntimeError as exc:
        assert str(exc) == 'native_rotary_projection_mapping_invalid'
    else:
        raise AssertionError('invalid mapping reused cached records')
    hal.values['v5-native-hal-owner.home-table-mapping-valid'] = True
    projection.project([0.0] * 5, [0.0] * 5)
    assert hal.read_count('v5-native-hal-owner.home-counts-per-unit-03') == 2


def main() -> int:
    check_router_zero_relative_counts_are_not_offset_twice()
    check_display_projection_rounds_fourth_digit_for_ui_only()
    check_one_count_displays_one_count_not_clamped()
    check_fractional_actual_count_is_preserved()
    check_fractional_command_phase_is_not_truncated()
    check_actual_projects_without_command_input()
    check_bc_mapping_projects_b_and_c()
    check_display_metadata_comes_from_active_model_scale()
    check_invalid_native_mapping_fails_closed()
    check_unconfigured_zero_table_keeps_native_display_available()
    check_inconsistent_wcheckpoint_window_fails_closed()
    check_wcheckpoint_valid_drop_fails_closed()
    check_stable_generation_uses_fast_metadata_path()
    check_generation_change_reloads_all_metadata_immediately()
    check_new_projection_instance_rebuilds_resident_cache()
    check_invalid_sample_drops_cache_before_same_generation_recovers()
    print('V5_MACHINE_STATUS_PROJECTION_TEST_OK')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
