#ifndef V5_BUS_AXIS_TARGET_LIMITER_H
#define V5_BUS_AXIS_TARGET_LIMITER_H

#include <float.h>
#include <stdint.h>

/*
 * Final physical-count envelope for one cyclic-position axis.  The normal
 * LinuxCNC trajectory is passed through when its next count is already inside
 * the velocity/acceleration envelope.  Any discontinuity is chased
 * automatically with a bounded braking profile.  While disabled, all state is
 * synchronized to actual position so re-enable never replays a stale target.
 *
 * The realtime caller supplies fabs/fmin/fmax/sqrt through rtapi_math.h; the
 * host smoke supplies them through math.h.
 */
static inline int v5_bus_axis_target_limiter_step(
    int enabled,
    int32_t desired_raw,
    int32_t actual_raw,
    double max_velocity_units_per_s,
    double max_acceleration_units_per_s2,
    double scale_counts_per_unit,
    double period_s,
    double *input_old,
    double *output,
    double *output_old,
    int *initialized,
    int32_t *limited_raw)
{
    double scale;
    double maxv;
    double maxa;
    double target;
    double input_velocity;
    double output_velocity;
    double min_velocity;
    double max_velocity;
    double quantized_velocity_tolerance;
    double target_velocity;
    double error;
    double next;
    int64_t rounded;
    int32_t output_low;
    int32_t desired_delta;

    if (!input_old || !output || !output_old || !initialized || !limited_raw)
        return 0;

    scale = fabs(scale_counts_per_unit);
    maxv = fabs(max_velocity_units_per_s) * scale;
    maxa = fabs(max_acceleration_units_per_s2) * scale;
    if (max_velocity_units_per_s != max_velocity_units_per_s ||
        max_acceleration_units_per_s2 != max_acceleration_units_per_s2 ||
        scale_counts_per_unit != scale_counts_per_unit ||
        period_s != period_s || scale <= 0.0 || maxv <= 0.0 ||
        maxa <= 0.0 || period_s <= 0.0 || maxv > DBL_MAX ||
        maxa > DBL_MAX) {
        *input_old = (double)actual_raw;
        *output = (double)actual_raw;
        *output_old = (double)actual_raw;
        *initialized = 0;
        *limited_raw = actual_raw;
        return 0;
    }

    if (!enabled || !*initialized) {
        *input_old = (double)actual_raw;
        *output = (double)actual_raw;
        *output_old = (double)actual_raw;
        *initialized = 1;
        *limited_raw = actual_raw;
        return 1;
    }

    /* Follow the nearest signed-32 representation across raw wrap. */
    rounded = *output >= 0.0 ? (int64_t)(*output + 0.5)
                             : (int64_t)(*output - 0.5);
    output_low = (int32_t)(uint32_t)(uint64_t)rounded;
    desired_delta = (int32_t)((uint32_t)desired_raw - (uint32_t)output_low);
    target = *output + (double)desired_delta;

    output_velocity = (*output - *output_old) / period_s;
    min_velocity = fmax(output_velocity - maxa * period_s, -maxv);
    max_velocity = fmin(output_velocity + maxa * period_s, maxv);
    input_velocity = (target - *input_old) / period_s;

    /*
     * Both cia402 and this final PDO owner operate on integer counts.  A
     * continuous trajectory that is exactly on a velocity/acceleration limit
     * can therefore alternate adjacent count deltas after rounding.  The
     * resulting apparent acceleration differs by at most two counts per
     * period; it is quantization, not a second motion command.  Use this
     * tolerance only to recognize the transparent path.  The discontinuity
     * chase below keeps the exact physical envelope.
     */
    quantized_velocity_tolerance = 2.0 / period_s;

    /* Transparent fast path for a trajectory that is already physically valid. */
    if (fabs(*output - *input_old) <= 1.0 &&
        input_velocity >= min_velocity - quantized_velocity_tolerance &&
        input_velocity <= max_velocity + quantized_velocity_tolerance) {
        next = target;
    } else {
        /*
         * Chase a discontinuity with the speed that can still brake inside the
         * remaining distance.  Acceleration is then clamped from the previous
         * output velocity, so neither a target jump nor a reversal can produce
         * an unbounded PDO step.
         */
        error = target - *output;
        if (fabs(error) <= 1e-12) {
            target_velocity = 0.0;
        } else {
            target_velocity = sqrt(2.0 * maxa * fabs(error));
            target_velocity = fmin(target_velocity, maxv);
            if (error < 0.0)
                target_velocity = -target_velocity;
        }
        target_velocity = fmin(max_velocity, fmax(min_velocity, target_velocity));
        next = *output + target_velocity * period_s;
    }

    if (next != next || next > 9.0e18 || next < -9.0e18) {
        *input_old = (double)actual_raw;
        *output = (double)actual_raw;
        *output_old = (double)actual_raw;
        *initialized = 0;
        *limited_raw = actual_raw;
        return 0;
    }

    *output_old = *output;
    *output = next;
    *input_old = target;
    rounded = next >= 0.0 ? (int64_t)(next + 0.5)
                          : (int64_t)(next - 0.5);
    *limited_raw = (int32_t)(uint32_t)(uint64_t)rounded;
    return 1;
}

#endif
