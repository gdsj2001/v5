#!/usr/bin/env python3
from __future__ import annotations

import math

from v5_machine_status_projection import NativeRotaryDisplayProjection


class FakeHal:
    def __init__(self, values):
        self.values = dict(values)

    def get_value(self, name):
        if name not in self.values:
            raise AssertionError(f'unexpected HAL read: {name}')
        return self.values[name]


class SequencedHal(FakeHal):
    def __init__(self, values, sequences):
        super().__init__(values)
        self.sequences = {name: list(items) for name, items in sequences.items()}

    def get_value(self, name):
        sequence = self.sequences.get(name)
        if sequence:
            return sequence.pop(0)
        return super().get_value(name)


def projection_values():
    values = {
        'v5-native-hal-owner.home-table-mapping-valid': True,
        'v5-native-hal-owner.home-table-map-gen': 0x46A18ECC,
        'v5-native-hal-owner.home-table-active-mask': 0x1F,
        'v5-native-hal-owner.home-table-commit-seq': 1,
    }
    axes = ((0, 'X', 0, 26, 10000), (1, 'Y', 1, 4, 10000),
            (2, 'Z', 2, 36, 10000), (3, 'A', 3, 5, 1000),
            (4, 'C', 4, 24, 1000))
    for joint, axis, slot, zero, scale in axes:
        suffix = f'{joint:02d}'
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


def check_one_count_displays_one_count_not_clamped() -> None:
    values = projection_values()
    values['v5-native-hal-owner.wcp-a-logical-counts'] = 1.0
    values['v5-native-hal-owner.wcp-a-runtime-counts'] = 1.0
    projection = NativeRotaryDisplayProjection(FakeHal(values))
    mcs, _ = projection.project(
        [0.0, 0.0, 0.0, 0.001, -18000.0],
        [0.0, 0.0, 0.0, 0.001, -18000.0])
    assert math.isclose(mcs[3], 0.001, abs_tol=1.0e-12)


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
    values['v5-native-hal-owner.home-axis-code-03'] = ord('B')
    for field in ('valid', 'generation', 'logical-counts', 'base-counts', 'runtime-counts'):
        values[f'v5-native-hal-owner.wcp-b-{field}'] = values.pop(
            f'v5-native-hal-owner.wcp-a-{field}')
    projection = NativeRotaryDisplayProjection(FakeHal(values))
    mcs, _ = projection.project(
        [0.0, 0.0, 0.0, 0.0, -18000.0], None)
    assert mcs[3] == 0.0 and mcs[4] == 0.0


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


def main() -> int:
    check_router_zero_relative_counts_are_not_offset_twice()
    check_one_count_displays_one_count_not_clamped()
    check_fractional_command_phase_is_not_truncated()
    check_actual_projects_without_command_input()
    check_bc_mapping_projects_b_and_c()
    check_invalid_native_mapping_fails_closed()
    check_inconsistent_wcheckpoint_window_fails_closed()
    check_wcheckpoint_valid_drop_fails_closed()
    print('V5_MACHINE_STATUS_PROJECTION_TEST_OK')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
