#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../../../linuxcnc/src/hal/components/v5_bus_axis_target_limiter.h"

typedef struct {
    double max_velocity;
    double max_acceleration;
    double scale;
} AxisLimits;

static double clamp_double(double value, double low, double high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

static int run_discontinuity_case(const AxisLimits *limits, int axis)
{
    const double period = 0.002;
    double input_old = 0.0;
    double output = 0.0;
    double output_old = 0.0;
    double previous_velocity = 0.0;
    int initialized = 0;
    int32_t limited = 0;
    int step;

    if (!v5_bus_axis_target_limiter_step(
            0, 0, 0, limits->max_velocity, limits->max_acceleration,
            limits->scale, period, &input_old, &output, &output_old,
            &initialized, &limited) || limited != 0) {
        fprintf(stderr, "axis %d disabled sync failed\n", axis);
        return 0;
    }

    for (step = 0; step < 2000; step++) {
        double before = output;
        double velocity;
        double acceleration;
        int32_t desired = step < 1000 ? 1000000000 : -1000000000;

        if (!v5_bus_axis_target_limiter_step(
                1, desired, limited, limits->max_velocity,
                limits->max_acceleration, limits->scale, period, &input_old,
                &output, &output_old, &initialized, &limited)) {
            fprintf(stderr, "axis %d limiter rejected valid owner limits\n", axis);
            return 0;
        }
        velocity = (output - before) / period;
        acceleration = (velocity - previous_velocity) / period;
        if (fabs(velocity) > fabs(limits->max_velocity * limits->scale) + 1e-4 ||
            fabs(acceleration) > fabs(limits->max_acceleration * limits->scale) + 1e-2) {
            fprintf(stderr,
                    "axis %d bounds failed at %d: v=%.9f a=%.9f\n",
                    axis, step, velocity, acceleration);
            return 0;
        }
        previous_velocity = velocity;
    }
    return 1;
}

static int run_transparent_case(void)
{
    const double period = 0.002;
    const double max_velocity = 100.0;
    const double max_acceleration = 500.0;
    const double scale = 10000.0;
    double input_old = 0.0;
    double output = 0.0;
    double output_old = 0.0;
    int initialized = 0;
    int32_t limited = 0;
    int32_t desired = 0;
    int step;

    if (!v5_bus_axis_target_limiter_step(
            0, 0, 0, max_velocity, max_acceleration, scale, period,
            &input_old, &output, &output_old, &initialized, &limited))
        return 0;
    for (step = 1; step <= 100; step++) {
        /* 20 counts/cycle is a constant 1 unit/s, safely inside all limits. */
        desired += 20;
        if (!v5_bus_axis_target_limiter_step(
                1, desired, limited, max_velocity, max_acceleration, scale,
                period, &input_old, &output, &output_old, &initialized,
                &limited) || limited != desired) {
            fprintf(stderr, "transparent trajectory changed at step %d\n", step);
            return 0;
        }
    }
    return 1;
}

static int run_quantized_planner_case(const AxisLimits *limits, int axis)
{
    const double period = 0.002;
    double input_old = 0.0;
    double output = 0.0;
    double output_old = 0.0;
    double position = 0.0;
    double velocity = 0.0;
    int initialized = 0;
    int32_t limited = 0;
    int step;

    if (!v5_bus_axis_target_limiter_step(
            0, 0, 0, limits->max_velocity, limits->max_acceleration,
            limits->scale, period, &input_old, &output, &output_old,
            &initialized, &limited))
        return 0;

    /*
     * Model the already-bounded LinuxCNC trajectory before PDO conversion.
     * llround() intentionally creates the one-count velocity/two-count
     * acceleration quantization that the final limiter must not reinterpret
     * as a physical discontinuity.
     */
    for (step = 0; step < 1800; step++) {
        double acceleration;
        int64_t desired_wide;
        int32_t desired;

        if (step < 450)
            acceleration = limits->max_acceleration;
        else if (step < 1350)
            acceleration = -limits->max_acceleration;
        else
            acceleration = limits->max_acceleration;

        velocity = clamp_double(
            velocity + acceleration * period,
            -limits->max_velocity,
            limits->max_velocity);
        position += velocity * period;
        desired_wide = (int64_t)llround(position * limits->scale);
        if (desired_wide > INT32_MAX || desired_wide < INT32_MIN) {
            fprintf(stderr, "axis %d planner fixture overflow at %d\n", axis, step);
            return 0;
        }
        desired = (int32_t)desired_wide;
        if (!v5_bus_axis_target_limiter_step(
                1, desired, limited, limits->max_velocity,
                limits->max_acceleration, limits->scale, period, &input_old,
                &output, &output_old, &initialized, &limited) ||
            limited != desired) {
            fprintf(stderr,
                    "axis %d quantized planner trajectory changed at %d: desired=%d limited=%d lag=%d\n",
                    axis, step, desired, limited, desired - limited);
            return 0;
        }
    }
    return 1;
}

static int run_wrap_and_invalid_cases(void)
{
    const double period = 0.002;
    double input_old = 0.0;
    double output = 0.0;
    double output_old = 0.0;
    int initialized = 0;
    int32_t limited = 0;

    if (!v5_bus_axis_target_limiter_step(
            0, INT32_MAX - 100, INT32_MAX - 100, 100.0, 500.0, 10000.0,
            period, &input_old, &output, &output_old, &initialized, &limited))
        return 0;
    if (!v5_bus_axis_target_limiter_step(
            1, INT32_MIN + 100, INT32_MAX - 100, 100.0, 500.0, 10000.0,
            period, &input_old, &output, &output_old, &initialized, &limited)) {
        fprintf(stderr, "signed-32 wrap case rejected\n");
        return 0;
    }
    if (limited == INT32_MIN + 100) {
        fprintf(stderr, "signed-32 wrap discontinuity bypassed envelope\n");
        return 0;
    }
    if (v5_bus_axis_target_limiter_step(
            1, 100, 42, 0.0, 500.0, 10000.0, period, &input_old, &output,
            &output_old, &initialized, &limited) || limited != 42) {
        fprintf(stderr, "invalid owner limits did not fail safe to actual\n");
        return 0;
    }
    return 1;
}

int main(void)
{
    const AxisLimits limits[5] = {
        {166.666666667, 500.0, 10000.0},
        {166.666666667, 500.0, 10000.0},
        {166.666666667, 500.0, 10000.0},
        {833.333333333, 2000.0, 10000.0},
        {833.333333333, 2000.0, 10000.0},
    };
    int axis;

    for (axis = 0; axis < 5; axis++) {
        if (!run_discontinuity_case(&limits[axis], axis))
            return EXIT_FAILURE;
        if (!run_quantized_planner_case(&limits[axis], axis))
            return EXIT_FAILURE;
    }
    if (!run_transparent_case() || !run_wrap_and_invalid_cases())
        return EXIT_FAILURE;
    puts("V5_BUS_AXIS_TARGET_LIMITER_SMOKE_OK");
    return EXIT_SUCCESS;
}
