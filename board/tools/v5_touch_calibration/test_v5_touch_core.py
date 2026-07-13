#!/usr/bin/env python3

import unittest

from v5_touch_core import EDGE_MAX_ERROR_PX, fit_affine, validate_fit


class TouchCoreCalibrationTest(unittest.TestCase):
    def assert_accepted(self, samples):
        coefficients = fit_affine(samples)
        average_error, maximum_error, edge_error = validate_fit(samples, coefficients)
        self.assertLess(maximum_error, 10.0)
        self.assertLess(edge_error, EDGE_MAX_ERROR_PX)
        return coefficients, average_error, maximum_error, edge_error

    def test_normal_axis_five_point_fit(self):
        samples = ((81, 79), (943, 82), (946, 519), (79, 522), (512, 300))
        self.assert_accepted(samples)

    def test_board_regression_swapped_axis_fit(self):
        samples = ((88, 81), (96, 937), (533, 936), (524, 77), (304, 513))
        coefficients, _average_error, _maximum_error, edge_error = self.assert_accepted(samples)
        self.assertGreater(abs(coefficients[1]), 0.9)
        self.assertGreater(abs(coefficients[3]), 0.9)
        self.assertLess(edge_error, 11.0)

    def test_reversed_axes_five_point_fit(self):
        samples = ((944, 520), (80, 520), (80, 80), (944, 80), (512, 300))
        self.assert_accepted(samples)


if __name__ == "__main__":
    unittest.main()
