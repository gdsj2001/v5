#include "v5_main_page.h"

#include <string.h>

static lv_coord_t clamp_coord(double value, lv_coord_t min_value, lv_coord_t max_value)
{
    if (value < (double)min_value) {
        return min_value;
    }
    if (value > (double)max_value) {
        return max_value;
    }
    return (lv_coord_t)value;
}


static void button_event_cb(lv_event_t *event)
{
    V5MainPage *page = (V5MainPage *)lv_event_get_user_data(event);
    lv_obj_t *target = lv_event_get_target(event);
    unsigned int i;

    if (!page || lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    for (i = 0; i < page->button_count; ++i) {
        if (page->buttons[i] == target) {
            (void)v5_main_page_trigger_action(page, page->button_actions[i], 0);
            return;
        }
    }
}

static lv_obj_t *make_button(V5MainPage *page, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, V5MainPageActionKind action)
{
    lv_obj_t *button;
    lv_obj_t *label;
    if (!page || page->button_count >= V5_MAIN_PAGE_BUTTON_COUNT) {
        return 0;
    }
    button = lv_btn_create(page->root);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, w, h);
    lv_obj_add_event_cb(button, button_event_cb, LV_EVENT_CLICKED, page);
    label = lv_label_create(button);
    lv_label_set_text(label, v5_main_page_action_label(action));
    lv_obj_center(label);
    page->buttons[page->button_count] = button;
    page->button_actions[page->button_count] = action;
    page->button_count += 1u;
    return button;
}

static lv_obj_t *make_label(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, w, 22);
    lv_label_set_text(label, text ? text : "--");
    return label;
}

void v5_main_page_init(V5MainPage *page)
{
    if (!page) {
        return;
    }
    memset(page, 0, sizeof(*page));
}

int v5_main_page_create(V5MainPage *page, lv_obj_t *parent)
{
    unsigned int i;
    static const char *axis_text[V5_MAIN_PAGE_AXIS_COUNT] = {"X", "Y", "Z", "A", "C"};

    if (!page || !parent) {
        return 0;
    }
    v5_main_page_init(page);

    page->root = lv_obj_create(parent);
    lv_obj_set_pos(page->root, 0, 0);
    lv_obj_set_size(page->root, 800, 480);
    lv_obj_clear_flag(page->root, LV_OBJ_FLAG_SCROLLABLE);

    make_label(page->root, 18, 12, 72, "AXIS");
    make_label(page->root, 82, 12, 126, "MCS");
    make_label(page->root, 214, 12, 126, "CMD");
    make_label(page->root, 346, 12, 126, "ERR");
    for (i = 0; i < V5_MAIN_PAGE_AXIS_COUNT; ++i) {
        lv_coord_t y = (lv_coord_t)(42 + (int)i * 34);
        page->axis_labels[i] = make_label(page->root, 18, y, 38, axis_text[i]);
        page->mcs_labels[i] = make_label(page->root, 82, y, 126, "--.---");
        page->cmd_labels[i] = make_label(page->root, 214, y, 126, "--.---");
        page->error_labels[i] = make_label(page->root, 346, y, 126, "--.---");
    }

    page->modal_label = make_label(page->root, 18, 228, 440, "--");
    page->spindle_speed_label = make_label(page->root, 500, 42, 160, "--");
    page->linear_velocity_label = make_label(page->root, 500, 76, 160, "--");
    page->feed_override_label = make_label(page->root, 500, 110, 160, "--");
    page->spindle_override_label = make_label(page->root, 500, 144, 160, "--");

    page->trajectory_line = lv_line_create(page->root);
    lv_obj_set_pos(page->trajectory_line, 500, 210);
    lv_obj_set_size(page->trajectory_line, 260, 190);
    page->mcs_marker_label = make_label(page->root, 500, 408, 90, "MCS --");
    page->cmd_marker_label = make_label(page->root, 610, 408, 90, "CMD --");

    make_button(page, 18, 270, 78, 32, V5_MAIN_PAGE_ACTION_START);
    make_button(page, 102, 270, 78, 32, V5_MAIN_PAGE_ACTION_PAUSE);
    make_button(page, 186, 270, 78, 32, V5_MAIN_PAGE_ACTION_RESUME);
    make_button(page, 270, 270, 78, 32, V5_MAIN_PAGE_ACTION_HOME);
    make_button(page, 354, 270, 78, 32, V5_MAIN_PAGE_ACTION_ESTOP_FORCE);
    make_button(page, 438, 270, 102, 32, V5_MAIN_PAGE_ACTION_ESTOP_RESET);

    make_button(page, 18, 310, 60, 30, V5_MAIN_PAGE_ACTION_WCS_G54);
    make_button(page, 84, 310, 60, 30, V5_MAIN_PAGE_ACTION_WCS_G55);
    make_button(page, 150, 310, 60, 30, V5_MAIN_PAGE_ACTION_WCS_G56);
    make_button(page, 216, 310, 60, 30, V5_MAIN_PAGE_ACTION_WCS_G57);
    make_button(page, 282, 310, 60, 30, V5_MAIN_PAGE_ACTION_WCS_G58);
    make_button(page, 348, 310, 60, 30, V5_MAIN_PAGE_ACTION_WCS_G59);
    make_button(page, 414, 310, 66, 30, V5_MAIN_PAGE_ACTION_WCS_G591);
    make_button(page, 486, 310, 66, 30, V5_MAIN_PAGE_ACTION_WCS_G592);
    make_button(page, 558, 310, 66, 30, V5_MAIN_PAGE_ACTION_WCS_G593);

    make_button(page, 18, 350, 86, 30, V5_MAIN_PAGE_ACTION_WORK_ZERO_X);
    make_button(page, 110, 350, 94, 30, V5_MAIN_PAGE_ACTION_G92_CLEAR);
    make_button(page, 210, 350, 88, 30, V5_MAIN_PAGE_ACTION_RTCP_ON);
    make_button(page, 304, 350, 88, 30, V5_MAIN_PAGE_ACTION_RTCP_OFF);
    make_button(page, 398, 350, 82, 30, V5_MAIN_PAGE_ACTION_FEED_OVERRIDE_100);
    make_button(page, 486, 350, 82, 30, V5_MAIN_PAGE_ACTION_SPINDLE_OVERRIDE_100);

    make_button(page, 18, 390, 58, 30, V5_MAIN_PAGE_ACTION_JOG_STEP_1);
    make_button(page, 82, 390, 58, 30, V5_MAIN_PAGE_ACTION_JOG_STEP_10);
    make_button(page, 146, 390, 64, 30, V5_MAIN_PAGE_ACTION_JOG_STEP_100);
    make_button(page, 220, 390, 54, 30, V5_MAIN_PAGE_ACTION_VIEW_XY);
    make_button(page, 280, 390, 54, 30, V5_MAIN_PAGE_ACTION_VIEW_XZ);
    make_button(page, 340, 390, 54, 30, V5_MAIN_PAGE_ACTION_VIEW_YZ);
    make_button(page, 400, 390, 54, 30, V5_MAIN_PAGE_ACTION_VIEW_3D);
    return page->button_count == V5_MAIN_PAGE_BUTTON_COUNT;
}

int v5_main_page_apply_status(V5MainPage *page, const V5UiStatusView *status)
{
    V5CoordinatePanelSnapshot panel;
    V5ToolpathDisplaySnapshot display;
    unsigned int i;

    if (!page || !page->root) {
        return 0;
    }

    v5_coordinate_panel_from_status(status, &panel);
    for (i = 0; i < V5_MAIN_PAGE_AXIS_COUNT; ++i) {
        if (page->axis_labels[i]) {
            char axis[2] = {panel.lines[i].axis ? panel.lines[i].axis : '-', '\0'};
            lv_label_set_text(page->axis_labels[i], axis);
        }
        if (page->mcs_labels[i]) {
            lv_label_set_text(page->mcs_labels[i], panel.lines[i].mcs_text);
        }
        if (page->cmd_labels[i]) {
            lv_label_set_text(page->cmd_labels[i], panel.lines[i].cmd_text);
        }
        if (page->error_labels[i]) {
            lv_label_set_text(page->error_labels[i], panel.lines[i].following_error_text);
        }
    }
    lv_label_set_text(page->modal_label, panel.modal_text);
    lv_label_set_text(page->spindle_speed_label, panel.spindle_speed_text);
    lv_label_set_text(page->linear_velocity_label, panel.linear_velocity_text);
    lv_label_set_text(page->feed_override_label, panel.feed_override_text);
    lv_label_set_text(page->spindle_override_label, panel.spindle_override_text);

    v5_toolpath_display_from_status(status, V5_TOOLPATH_DISPLAY_XY, 260.0, 190.0, &display);
    page->trajectory_point_count = display.point_count;
    if (display.trajectory_valid && display.point_count > 0u) {
        for (i = 0; i < display.point_count; ++i) {
            page->trajectory_points[i].x = clamp_coord(display.trajectory[i].x, 0, 260);
            page->trajectory_points[i].y = clamp_coord(display.trajectory[i].y, 0, 190);
        }
        lv_line_set_points(page->trajectory_line, page->trajectory_points, (uint16_t)display.point_count);
    } else {
        page->trajectory_points[0].x = 0;
        page->trajectory_points[0].y = 0;
        page->trajectory_point_count = 0u;
        lv_line_set_points(page->trajectory_line, page->trajectory_points, 1);
    }

    if (display.mcs_valid) {
        lv_obj_set_pos(page->mcs_marker_label, (lv_coord_t)(500 + clamp_coord(display.mcs_point.x, 0, 260)), (lv_coord_t)(210 + clamp_coord(display.mcs_point.y, 0, 190)));
        lv_label_set_text(page->mcs_marker_label, "MCS");
    } else {
        lv_label_set_text(page->mcs_marker_label, "MCS --");
    }
    if (display.cmd_valid) {
        lv_obj_set_pos(page->cmd_marker_label, (lv_coord_t)(500 + clamp_coord(display.cmd_point.x, 0, 260)), (lv_coord_t)(210 + clamp_coord(display.cmd_point.y, 0, 190)));
        lv_label_set_text(page->cmd_marker_label, "CMD");
    } else {
        lv_label_set_text(page->cmd_marker_label, "CMD --");
    }

    return 1;
}

void v5_main_page_bind_program_controller(V5MainPage *page, V5ProgramController *controller)
{
    if (!page) {
        return;
    }
    page->program_controller = controller;
}

int v5_main_page_open_program(V5MainPage *page, const char *path, V5ProgramOpenResult *open_report)
{
    V5ProgramOpenResult local_open;
    V5ProgramOpenResult *out = open_report ? open_report : &local_open;

    if (!page || !page->program_controller) {
        return 0;
    }
    if (!v5_command_program_open(page->program_controller, path, out)) {
        return 0;
    }
    page->last_program_open = *out;
    return 1;
}

int v5_main_page_trigger_action(V5MainPage *page, V5MainPageActionKind action, V5MainPageActionReport *report)
{
    V5MainPageActionReport local_report;
    V5MainPageActionReport *out = report ? report : &local_report;
    const V5ProgramRuntime *runtime;

    if (!page) {
        return 0;
    }
    runtime = page->program_controller ?
        v5_program_controller_runtime(page->program_controller) : 0;

    if (!v5_main_page_action_prepare(action, runtime, out)) {
        return 0;
    }
    page->last_action = *out;
    return 1;
}
