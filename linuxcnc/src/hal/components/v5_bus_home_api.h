#ifndef V5_BUS_HOME_API_H
#define V5_BUS_HOME_API_H

bool get_allhomed(void)
{
    int joint;
    int required = 0;
    for (joint = 0; joint < all_joints && joint < V5_HOME_JOINTS; ++joint) {
        if (!GET_JOINT_ACTIVE_FLAG(&joints[joint]) ||
            !v5_home_sequence_valid(H[joint].home_sequence)) continue;
        required = 1;
        if (!base_get_homed(joint)) return 0;
    }
    return required ? 1 : 0;
}
bool homing_wcheckpoint_reset_required(void) { return 0; }
bool get_homed(int j) { return base_get_homed(j); }
bool get_home_is_idle(int j) { return base_get_home_is_idle(j); }
bool get_home_is_synchronized(int j) { return base_get_home_is_synchronized(j); }
bool get_home_needs_unlock_first(int j) { return base_get_home_needs_unlock_first(j); }
int get_home_sequence(int j) { return base_get_home_sequence(j); }
bool get_homing(int j)
{
    return j >= 0 && j < V5_HOME_JOINTS && (run_mask & (1u << j)) &&
        txn_phase > V5_TXN_IDLE && txn_phase < V5_TXN_TERMINAL
        ? 1 : base_get_homing(j);
}
bool get_homing_at_index_search_wait(int j)
{
    return base_get_homing_at_index_search_wait(j);
}
bool get_homing_is_active(void)
{
    return !base_arming &&
        txn_phase > V5_TXN_IDLE && txn_phase < V5_TXN_TERMINAL
        ? 1 : base_get_homing_is_active();
}
bool get_index_enable(int j) { return base_get_index_enable(j); }
void set_unhomed(int j, motion_state_t state) { base_set_unhomed(j, state); }

void set_joint_homing_params(
    int j, double offset, double home, double final_vel, double search_vel,
    double latch_vel, int flags, int sequence, bool volatile_home)
{
    base_set_joint_homing_params(
        j, offset, home, final_vel, search_vel, latch_vel, flags, sequence, volatile_home);
}

void update_joint_homing_params(int j, double offset, double home, int sequence)
{
    base_update_joint_homing_params(j, offset, home, sequence);
}

#define V5_HOME_EXPORT(symbol) EXPORT_SYMBOL(symbol)
V5_HOME_EXPORT(homeMotFunctions);
V5_HOME_EXPORT(homing_init);
V5_HOME_EXPORT(do_homing);
V5_HOME_EXPORT(homing_wcheckpoint_reset_required);
V5_HOME_EXPORT(get_allhomed);
V5_HOME_EXPORT(get_homed);
V5_HOME_EXPORT(get_home_is_idle);
V5_HOME_EXPORT(get_home_is_synchronized);
V5_HOME_EXPORT(get_home_needs_unlock_first);
V5_HOME_EXPORT(get_home_sequence);
V5_HOME_EXPORT(get_homing);
V5_HOME_EXPORT(get_homing_at_index_search_wait);
V5_HOME_EXPORT(get_homing_is_active);
V5_HOME_EXPORT(get_index_enable);
V5_HOME_EXPORT(read_homing_in_pins);
V5_HOME_EXPORT(do_home_joint);
V5_HOME_EXPORT(do_cancel_homing);
V5_HOME_EXPORT(set_unhomed);
V5_HOME_EXPORT(set_joint_homing_params);
V5_HOME_EXPORT(update_joint_homing_params);
V5_HOME_EXPORT(write_homing_out_pins);
#undef V5_HOME_EXPORT

#endif
