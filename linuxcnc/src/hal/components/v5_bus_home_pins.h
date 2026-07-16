#ifndef V5_BUS_HOME_PINS_H
#define V5_BUS_HOME_PINS_H

static int v5_home_make_pins(int id, int njoints)
{
    static const char rotary_name[3] = {'a', 'b', 'c'};
    int j, rc = 0;
    P = hal_malloc(sizeof(*P));
    if (!P) return -1;
    for (j = 0; j < njoints; ++j) {
        v5_joint_pins *p = &P->joint[j];
        rc += hal_pin_bit_newf(HAL_IN, &p->config_valid, id, "joint.%d.v5-bus-home-config-valid", j);
        rc += hal_pin_u32_newf(HAL_IN, &p->status_slot, id, "joint.%d.v5-bus-home-status-slot", j);
        rc += hal_pin_u32_newf(HAL_IN, &p->axis_code, id, "joint.%d.v5-bus-home-axis-code", j);
        rc += hal_pin_u32_newf(HAL_IN, &p->slave_position, id, "joint.%d.v5-bus-home-slave-position", j);
        rc += hal_pin_u32_newf(HAL_IN, &p->mapping_generation, id, "joint.%d.v5-bus-home-mapping-generation", j);
        rc += hal_pin_float_newf(HAL_IN, &p->counts_per_unit, id, "joint.%d.v5-bus-home-counts-per-unit", j);
        rc += hal_pin_s32_newf(HAL_IN, &p->actual_counts, id, "joint.%d.v5-bus-home-actual-counts", j);
        rc += hal_pin_s32_newf(HAL_OUT, &p->phase, id, "joint.%d.v5-bus-home-phase", j);
        rc += hal_pin_s32_newf(HAL_OUT, &p->failure, id, "joint.%d.v5-bus-home-failure", j);
        rc += hal_pin_float_newf(HAL_OUT, &p->start_counts, id, "joint.%d.v5-bus-home-start-counts", j);
        rc += hal_pin_float_newf(HAL_OUT, &p->target_counts, id, "joint.%d.v5-bus-home-target-counts", j);
        rc += hal_pin_float_newf(HAL_OUT, &p->readback_counts, id, "joint.%d.v5-bus-home-readback-counts", j);
        rc += hal_pin_float_newf(HAL_OUT, &p->error_counts, id, "joint.%d.v5-bus-home-error-counts", j);
        rc += hal_pin_u32_newf(HAL_OUT, &p->readback_generation, id, "joint.%d.v5-bus-home-readback-generation", j);
        rc += hal_pin_bit_newf(HAL_OUT, &p->readback_valid, id, "joint.%d.v5-bus-home-readback-valid", j);
        rc += hal_pin_bit_newf(HAL_OUT, &p->motion_active, id, "joint.%d.v5-bus-home-motion-active", j);
        rc += hal_pin_u32_newf(HAL_OUT, &p->transaction, id, "joint.%d.v5-bus-home-transaction", j);
    }
    for (j = 0; j < 3; ++j) {
        v5_wcp_pins *p = &P->rotary[j];
        rc += hal_pin_float_newf(HAL_IN, &p->base_counts, id, "motion.v5-bus-home-wcp-%c-base-counts", rotary_name[j]);
        rc += hal_pin_u32_newf(HAL_IN, &p->generation, id, "motion.v5-bus-home-wcp-%c-generation", rotary_name[j]);
        rc += hal_pin_bit_newf(HAL_IN, &p->valid, id, "motion.v5-bus-home-wcp-%c-valid", rotary_name[j]);
    }
#define V5_HOME_NEW_BIT(field, name) rc += hal_pin_bit_newf(HAL_IN, &P->field, id, name)
#define V5_HOME_NEW_U32(field, name) rc += hal_pin_u32_newf(HAL_IN, &P->field, id, name)
    V5_HOME_NEW_BIT(config_mapping_valid, "motion.v5-bus-home-config-mapping-valid");
    V5_HOME_NEW_U32(config_mapping_generation, "motion.v5-bus-home-config-mapping-generation");
    V5_HOME_NEW_U32(config_active_mask, "motion.v5-bus-home-config-active-mask");
    V5_HOME_NEW_U32(config_commit_seq, "motion.v5-bus-home-config-commit-seq");
    V5_HOME_NEW_BIT(router_mapping_valid, "motion.v5-bus-home-router-mapping-valid");
    V5_HOME_NEW_U32(router_mapping_generation, "motion.v5-bus-home-router-mapping-generation");
    V5_HOME_NEW_U32(router_active_mask, "motion.v5-bus-home-router-active-mask");
    V5_HOME_NEW_U32(router_commit_seq, "motion.v5-bus-home-router-commit-seq");
    V5_HOME_NEW_BIT(safety_actual_valid, "motion.v5-bus-home-safety-actual-valid");
    V5_HOME_NEW_BIT(estop_active, "motion.v5-bus-home-estop-active");
    V5_HOME_NEW_BIT(machine_enabled, "motion.v5-bus-home-machine-enabled");
    V5_HOME_NEW_BIT(rtcp_actual_valid, "motion.v5-bus-home-rtcp-actual-valid");
    V5_HOME_NEW_BIT(rtcp_force_latched, "motion.v5-bus-home-rtcp-force-latched");
    V5_HOME_NEW_U32(rtcp_latched_transaction, "motion.v5-bus-home-rtcp-latched-transaction");
    rc += hal_pin_float_newf(HAL_IN, &P->rtcp_active, id, "motion.v5-bus-home-rtcp-active");
    rc += hal_pin_bit_newf(HAL_OUT, &P->rtcp_force_off, id, "motion.v5-bus-home-rtcp-force-off");
    rc += hal_pin_u32_newf(HAL_OUT, &P->rtcp_request_transaction, id, "motion.v5-bus-home-rtcp-request-transaction");
    rc += hal_pin_u32_newf(HAL_OUT, &P->transaction, id, "motion.v5-bus-home-transaction");
    rc += hal_pin_u32_newf(HAL_OUT, &P->run_active_mask, id, "motion.v5-bus-home-run-active-mask");
    rc += hal_pin_u32_newf(HAL_OUT, &P->completed_mask, id, "motion.v5-bus-home-completed-mask");
    rc += hal_pin_u32_newf(HAL_OUT, &P->terminal_mask, id, "motion.v5-bus-home-terminal-mask");
    rc += hal_pin_u32_newf(HAL_OUT, &P->failed_joint, id, "motion.v5-bus-home-failed-joint");
    rc += hal_pin_s32_newf(HAL_OUT, &P->failure_code, id, "motion.v5-bus-home-failure-code");
    rc += hal_pin_s32_newf(HAL_OUT, &P->failure_phase, id, "motion.v5-bus-home-failure-phase");
    rc += hal_pin_u32_newf(HAL_OUT, &P->failure_current_mask, id, "motion.v5-bus-home-failure-current-mask");
    rc += hal_pin_float_newf(HAL_OUT, &P->failure_actual_counts, id, "motion.v5-bus-home-failure-actual-counts");
    rc += hal_pin_float_newf(HAL_OUT, &P->failure_target_counts, id, "motion.v5-bus-home-failure-target-counts");
    rc += hal_pin_float_newf(HAL_OUT, &P->failure_error_counts, id, "motion.v5-bus-home-failure-error-counts");
    rc += hal_pin_u32_newf(HAL_OUT, &P->failure_generation, id, "motion.v5-bus-home-failure-generation");
    rc += hal_pin_bit_newf(HAL_OUT, &P->failure_readback_valid, id, "motion.v5-bus-home-failure-readback-valid");
    rc += hal_pin_bit_newf(HAL_OUT, &P->failure_motion_active, id, "motion.v5-bus-home-failure-motion-active");
#undef V5_HOME_NEW_BIT
#undef V5_HOME_NEW_U32
    return rc ? -1 : 0;
}

#endif
