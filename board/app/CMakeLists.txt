set(V5_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(V5_LVGL_DIR "${V5_ROOT_DIR}/third_party/lvgl-v8.3.11")

if(POLICY CMP0177)
    set(CMAKE_POLICY_DEFAULT_CMP0177 NEW)
endif()

set(LV_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE STRING "v5 LVGL config" FORCE)
set(LV_CONF_INCLUDE_SIMPLE ON CACHE BOOL "Use simple lv_conf include" FORCE)
set(LV_LVGL_H_INCLUDE_SIMPLE ON CACHE BOOL "Use simple LVGL include" FORCE)

add_subdirectory("${V5_LVGL_DIR}" "${CMAKE_BINARY_DIR}/third_party/lvgl" EXCLUDE_FROM_ALL)

if(UNIX)
    add_library(v5_remote_ui_packer SHARED
        ../services/ui/v5_remote_ui_native_packer.c
    )
    target_include_directories(v5_remote_ui_packer PUBLIC
        "${V5_ROOT_DIR}/services/ui"
    )
    set_target_properties(v5_remote_ui_packer PROPERTIES
        PREFIX ""
        OUTPUT_NAME "v5_remote_ui_packer"
        SUFFIX ""
    )

    add_executable(v5_remote_ui_native_packer_smoke
        ../services/ui/v5_remote_ui_native_packer_smoke.c
    )
    target_link_libraries(v5_remote_ui_native_packer_smoke PRIVATE v5_remote_ui_packer)
endif()

set(V5_PROGRAM_SCENE_PRODUCER_SOURCES
    ../services/state_publisher/v5_program_scene_producer.c
    ../services/state_publisher/v5_program_scene_projection.c
    ../services/state_publisher/v5_program_scene_cache.c
    ../services/state_publisher/v5_program_scene_model.c
    ../services/state_publisher/v5_program_scene_model_ac.c
    ../services/state_publisher/v5_program_scene_model_bc.c
    ../services/state_publisher/v5_program_scene_ipc.c
)

add_library(v5_button_visuals STATIC
    src/v5_button_visuals.c
)

add_library(v5_lvgl_clock STATIC
    src/v5_lvgl_clock.c
)

target_include_directories(v5_button_visuals PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_include_directories(v5_lvgl_clock PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_definitions(v5_button_visuals PRIVATE
    LV_CONF_INCLUDE_SIMPLE
    LV_LVGL_H_INCLUDE_SIMPLE
)

target_compile_definitions(v5_lvgl_clock PRIVATE
    LV_CONF_INCLUDE_SIMPLE
    LV_LVGL_H_INCLUDE_SIMPLE
)

target_link_libraries(v5_button_visuals PUBLIC lvgl)
target_link_libraries(v5_lvgl_clock PUBLIC lvgl)

add_executable(v5_coordinate_digits_smoke
    src/lv_font_zh_16.c
    src/v5_coordinate_digits_smoke.c
    src/v5_coordinate_digits.c
    src/v5_lvgl_headless.c
)

target_include_directories(v5_coordinate_digits_smoke PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_definitions(v5_coordinate_digits_smoke PRIVATE
    LV_CONF_INCLUDE_SIMPLE
    LV_LVGL_H_INCLUDE_SIMPLE
)

target_link_libraries(v5_coordinate_digits_smoke PRIVATE lvgl)

add_executable(v5_program_scene_producer_smoke
    src/v5_program_scene_producer_smoke.c
    ${V5_PROGRAM_SCENE_PRODUCER_SOURCES}
    ../services/command_gate/v5_native_readback.c
)

target_include_directories(v5_program_scene_producer_smoke PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${V5_ROOT_DIR}/services/command_gate"
    "${V5_ROOT_DIR}/services/state_publisher"
)

if(NOT WIN32)
    target_link_libraries(v5_program_scene_producer_smoke PRIVATE m)
endif()

add_executable(v5_lvgl_remote_display_delta_smoke
    src/v5_lvgl_remote_display_delta_smoke.c
    src/v5_lvgl_remote_display_delta.c
)

target_include_directories(v5_lvgl_remote_display_delta_smoke PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

if(UNIX)
add_executable(v5_lvgl_shell
    src/lv_font_zh_16.c
    src/v5_layout_icon_masks.c
    src/v5_layout_icons.c
    src/main.c
    src/v5_process_residency.c
    src/v5_boot_closure.c
    src/v5_coordinate_panel.c
    src/v5_coordinate_digits.c
    src/v5_lvgl_headless.c
    src/v5_lvgl_remote_display.c
    src/v5_lvgl_remote_display_delta.c
    src/v5_lvgl_remote_display_capture.c
    src/v5_ui_first_frame_guard.c
    src/v5_lvgl_remote_input.c
    src/v5_remote_input_ipc.c
    src/v5_remote_metrics.c
    src/v5_lvgl_touch_input.c
    src/v5_network_status.c
    src/v5_main_page.c
    src/v5_main_page_status_text.c
    src/v5_main_page_toolpath_primitives.c
    src/v5_main_page_toolpath_geometry.c
    src/v5_main_page_toolpath_program.c
    src/v5_main_page_toolpath_raster.c
    src/v5_main_page_program_preview.c
    src/v5_main_page_input.c
    src/v5_main_page_override.c
    src/v5_main_page_action_guard.c
    src/v5_main_page_button_state.c
    src/v5_main_page_view.c
    src/v5_main_page_program_state.c
    src/v5_main_page_action_execute.c
    src/v5_main_page_home_transaction.c
    src/v5_settings_page.c
    src/v5_settings_page_support.c
    src/v5_settings_page_status.c
    src/v5_settings_page_actions.c
    src/v5_settings_page_widgets.c
    src/v5_runtime_registry.c
    src/v5_settings_parameter_store.c
    src/v5_settings_axis_table.c
    src/v5_settings_axis_table_resident.c
    src/v5_settings_axis_table_runtime.c
    src/v5_settings_axis_table_readback.c
    src/v5_settings_axis_table_widgets.c
    src/v5_settings_axis_table_commit.c
    src/v5_settings_axis_table_options.c
    src/v5_settings_axis_table_dropdown.c
    src/v5_settings_axis_table_keyboard.c
    src/v5_settings_axis_table_layout.c
    ../services/drive_profile/v5_drive_profile_snapshot.c
    src/v5_settings_actions.c
    src/v5_settings_action_ipc.c
    src/v5_main_page_actions.c
    src/v5_parameter_table.c
    src/v5_ui_model.c
    src/v5_ui_shell.c
    src/v5_ui_shell_status.c
    src/v5_ui_shell_operator_error.c
    src/v5_ui_shell_program_scan.c
    src/v5_ui_shell_program_actions.c
    src/v5_ui_shell_program_delete.c
    src/v5_ui_shell_pages.c
    src/v5_ui_shell_mdi_editor.c
    src/v5_ui_shell_navigation.c
    src/v5_ui_shell_boot_cache.c
    src/v5_ui_shell_bootstrap.c
    src/v5_ui_shell_refresh.c
    src/v5_v3_page_widgets.c
    src/v5_v3_tool_page.c
    src/v5_v3_offset_page.c
    src/v5_ui_status_view.c
)

add_executable(v5_program_page_double_click_smoke
    src/lv_font_zh_16.c
    src/v5_layout_icon_masks.c
    src/v5_layout_icons.c
    src/v5_program_page_double_click_smoke.c
    src/v5_boot_closure.c
    src/v5_coordinate_panel.c
    src/v5_coordinate_digits.c
    src/v5_lvgl_headless.c
    src/v5_lvgl_remote_display.c
    src/v5_lvgl_remote_display_delta.c
    src/v5_lvgl_remote_display_capture.c
    src/v5_ui_first_frame_guard.c
    src/v5_lvgl_remote_input.c
    src/v5_remote_input_ipc.c
    src/v5_remote_metrics.c
    src/v5_lvgl_touch_input.c
    src/v5_network_status.c
    src/v5_main_page.c
    src/v5_main_page_status_text.c
    src/v5_main_page_toolpath_primitives.c
    src/v5_main_page_toolpath_geometry.c
    src/v5_main_page_toolpath_program.c
    src/v5_main_page_toolpath_raster.c
    src/v5_main_page_program_preview.c
    src/v5_main_page_input.c
    src/v5_main_page_override.c
    src/v5_main_page_action_guard.c
    src/v5_main_page_button_state.c
    src/v5_main_page_view.c
    src/v5_main_page_program_state.c
    src/v5_main_page_action_execute.c
    src/v5_main_page_home_transaction.c
    src/v5_settings_page.c
    src/v5_settings_page_support.c
    src/v5_settings_page_status.c
    src/v5_settings_page_actions.c
    src/v5_settings_page_widgets.c
    src/v5_runtime_registry.c
    src/v5_settings_axis_table.c
    src/v5_settings_axis_table_resident.c
    src/v5_settings_axis_table_runtime.c
    src/v5_settings_axis_table_readback.c
    src/v5_settings_axis_table_widgets.c
    src/v5_settings_axis_table_commit.c
    src/v5_settings_axis_table_options.c
    src/v5_settings_axis_table_dropdown.c
    src/v5_settings_axis_table_keyboard.c
    src/v5_settings_axis_table_layout.c
    ../services/drive_profile/v5_drive_profile_snapshot.c
    src/v5_settings_actions.c
    src/v5_settings_action_ipc.c
    src/v5_main_page_actions.c
    src/v5_parameter_table.c
    src/v5_ui_model.c
    src/v5_ui_shell.c
    src/v5_ui_shell_status.c
    src/v5_ui_shell_operator_error.c
    src/v5_ui_shell_program_scan.c
    src/v5_ui_shell_program_actions.c
    src/v5_ui_shell_program_delete.c
    src/v5_ui_shell_pages.c
    src/v5_ui_shell_mdi_editor.c
    src/v5_ui_shell_navigation.c
    src/v5_ui_shell_boot_cache.c
    src/v5_ui_shell_bootstrap.c
    src/v5_ui_shell_refresh.c
    src/v5_v3_page_widgets.c
    src/v5_v3_tool_page.c
    src/v5_v3_offset_page.c
    src/v5_ui_status_view.c
)
endif()

add_library(v5_operator_error_status STATIC
    ../services/state_publisher/v5_native_operator_error_status.c
)

target_include_directories(v5_operator_error_status PUBLIC
    "${V5_ROOT_DIR}/services/state_publisher"
)

add_executable(v5_ui_shell_operator_error_smoke
    src/lv_font_zh_16.c
    src/v5_ui_shell_operator_error_smoke.c
    src/v5_ui_shell_operator_error.c
    src/v5_lvgl_headless.c
)

target_include_directories(v5_ui_shell_operator_error_smoke PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${V5_ROOT_DIR}/services/command_gate"
    "${V5_ROOT_DIR}/services/program_runtime"
    "${V5_ROOT_DIR}/services/drive_profile"
    "${V5_ROOT_DIR}/services/microkernel"
    "${V5_ROOT_DIR}/services/state_publisher"
)

target_compile_definitions(v5_ui_shell_operator_error_smoke PRIVATE
    LV_CONF_INCLUDE_SIMPLE
    LV_LVGL_H_INCLUDE_SIMPLE
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(v5_ui_shell_operator_error_smoke PRIVATE
    lvgl
    v5_button_visuals
    v5_operator_error_status
)

if(UNIX)
add_executable(v5_ui_shell_program_delete_smoke
    src/lv_font_zh_16.c
    src/v5_ui_shell_program_delete_smoke.c
    src/v5_ui_shell_program_delete.c
    src/v5_lvgl_headless.c
)

target_include_directories(v5_ui_shell_program_delete_smoke PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${V5_ROOT_DIR}/services/command_gate"
    "${V5_ROOT_DIR}/services/program_runtime"
    "${V5_ROOT_DIR}/services/drive_profile"
    "${V5_ROOT_DIR}/services/microkernel"
    "${V5_ROOT_DIR}/services/state_publisher"
)

target_compile_definitions(v5_ui_shell_program_delete_smoke PRIVATE
    LV_CONF_INCLUDE_SIMPLE
    LV_LVGL_H_INCLUDE_SIMPLE
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(v5_ui_shell_program_delete_smoke PRIVATE
    lvgl
    v5_button_visuals
)
endif()

add_executable(v5_ui_first_frame_guard_smoke
    src/lv_font_zh_16.c
    src/v5_ui_first_frame_guard_smoke.c
    src/v5_ui_first_frame_guard.c
    src/v5_lvgl_headless.c
)

target_include_directories(v5_ui_first_frame_guard_smoke PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_definitions(v5_ui_first_frame_guard_smoke PRIVATE
    LV_CONF_INCLUDE_SIMPLE
    LV_LVGL_H_INCLUDE_SIMPLE
)

target_link_libraries(v5_ui_first_frame_guard_smoke PRIVATE lvgl)

add_executable(v5_ui_page_cache_registry_smoke
    src/v5_ui_page_cache_registry_smoke.c
)

target_include_directories(v5_ui_page_cache_registry_smoke PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

if(UNIX)
add_library(v5_status_shm_projection STATIC
    src/v5_status_shm_reader.c
    src/v5_bus_status_reader.c
    ../services/state_publisher/v5_cpu_usage_snapshot.c
    ../services/state_publisher/v5_native_sample.c
    ../services/state_publisher/v5_state_publisher_service.c
    ../services/state_publisher/v5_status_shm_mmap.c
    ../services/state_publisher/v5_status_shm_writer.c
    ${V5_PROGRAM_SCENE_PRODUCER_SOURCES}
    ../services/command_gate/v5_native_readback.c
    ../services/command_gate/v5_native_rtcp_status.c
    ../services/command_gate/v5_native_wcs_status.c
    ../services/command_gate/v5_native_g53_geometry_status.c
    ../services/command_gate/v5_native_modal_tool_status.c
)

target_link_libraries(v5_status_shm_projection PUBLIC v5_operator_error_status m)

target_include_directories(v5_status_shm_projection PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${V5_ROOT_DIR}/services/state_publisher"
    "${V5_ROOT_DIR}/services/command_gate"
)

add_executable(v5_cpu_usage_snapshot_smoke
    ../services/state_publisher/v5_cpu_usage_snapshot.c
    ../services/state_publisher/v5_cpu_usage_snapshot_smoke.c
)

target_include_directories(v5_cpu_usage_snapshot_smoke PRIVATE
    "${V5_ROOT_DIR}/services/state_publisher"
)

if(NOT WIN32)
    target_link_libraries(v5_cpu_usage_snapshot_smoke PRIVATE m)
endif()

add_executable(v5_state_publisher_smoke
    ../services/state_publisher/v5_state_publisher.c
)

target_link_libraries(v5_state_publisher_smoke PRIVATE v5_status_shm_projection)

add_executable(v5_state_publisher
    ../services/state_publisher/v5_state_publisher_main.c
    src/v5_process_residency.c
)

target_link_libraries(v5_state_publisher PRIVATE v5_status_shm_projection)
endif()

add_executable(v5_state_publisher_cadence_smoke
    ../services/state_publisher/v5_state_publisher_cadence_smoke.c
)

target_include_directories(v5_state_publisher_cadence_smoke PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${V5_ROOT_DIR}/services/command_gate"
    "${V5_ROOT_DIR}/services/state_publisher"
)

add_library(v5_position_status_publisher_core STATIC
    ../services/state_publisher/v5_position_status_publisher_core.c
)

target_include_directories(v5_position_status_publisher_core PUBLIC
    "${V5_ROOT_DIR}/services/state_publisher"
)

if(NOT WIN32)
    target_link_libraries(v5_position_status_publisher_core PUBLIC m)
endif()

add_executable(v5_position_status_publisher_smoke
    ../services/state_publisher/v5_position_status_publisher_smoke.c
)

target_link_libraries(v5_position_status_publisher_smoke PRIVATE
    v5_position_status_publisher_core
)

if(UNIX)
    add_executable(v5_position_status_publisher
        ../services/state_publisher/v5_position_status_publisher.c
        ../services/state_publisher/v5_position_status_sampler.c
    )
    target_link_libraries(v5_position_status_publisher PRIVATE
        v5_position_status_publisher_core
        ${CMAKE_DL_LIBS}
    )
endif()

if(UNIX)
add_executable(v5_touch_diagnostics
    ../services/ui/v5_touch_diagnostics.c
)
endif()

add_library(v5_command_gate_client STATIC
    ../services/command_gate/v5_command_gate.c
    ../services/command_gate/v5_command_table.c
    ../services/command_gate/v5_command_gate_ipc.c
)

add_library(v5_linuxcncrsh_format STATIC
    ../services/command_gate/v5_linuxcncrsh_format.c
)

target_include_directories(v5_linuxcncrsh_format PUBLIC
    "${V5_ROOT_DIR}/services/command_gate"
)

target_include_directories(v5_command_gate_client PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${V5_ROOT_DIR}/services/command_gate"
)

if(UNIX)
add_library(v5_command_gate_core STATIC
    src/v5_parameter_table.c
    src/v5_settings_parameter_store.c
    ../services/command_gate/v5_command_gate_validator.c
    ../services/command_gate/v5_linuxcncrsh_client.c
    ../services/command_gate/v5_linuxcncrsh_transport.c
    ../services/command_gate/v5_linuxcncrsh_state.c
    ../services/command_gate/v5_linuxcncrsh_position.c
    ../services/command_gate/v5_linuxcncrsh_format.c
    ../services/state_publisher/v5_native_sample.c
    ../services/command_gate/v5_native_first_point.c
    ../services/command_gate/v5_native_home.c
    ../services/command_gate/v5_native_home_runtime_owner.c
    ../services/command_gate/v5_native_home_mapping.c
    ../services/command_gate/v5_native_axis_zero_live.c
    ../services/command_gate/v5_native_axis_zero_position.c
    ../services/command_gate/v5_native_motion_parameters.c
    ../services/command_gate/v5_native_readback.c
    ../services/command_gate/v5_native_hal_owner_client.c
    ../services/command_gate/v5_native_rtcp_control.c
    ../services/command_gate/v5_native_rtcp_status.c
    ../services/command_gate/v5_native_wcs_status.c
    ../services/command_gate/v5_native_g53_geometry_status.c
    ../services/command_gate/v5_native_modal_tool_status.c
    ../services/command_gate/v5_native_safety.c
    ../services/command_gate/v5_native_work_zero.c
    ../services/command_gate/v5_parameter_owner_map.c
    ../services/command_gate/v5_settings_apply.c
    ../services/command_gate/v5_settings_apply_json.c
    ../services/command_gate/v5_settings_apply_ini_numeric.c
    ../services/command_gate/v5_settings_apply_scale.c
    ../services/command_gate/v5_settings_apply_values.c
    ../services/command_gate/v5_settings_apply_ini_text.c
    ../services/command_gate/v5_settings_apply_runtime.c
    ../services/command_gate/v5_settings_apply_model.c
    ../services/command_gate/v5_settings_apply_geometry.c
)

target_include_directories(v5_command_gate_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${V5_ROOT_DIR}/services/command_gate"
    "${V5_ROOT_DIR}/services/state_publisher"
    "${V5_ROOT_DIR}/../linuxcnc/src/hal/user_comps"
)

target_link_libraries(v5_command_gate_core PUBLIC v5_command_gate_client v5_operator_error_status m)
endif()


add_library(v5_microkernel_contract STATIC
    ../services/microkernel/v5_microkernel_manifest.c
    ../services/microkernel/v5_native_gate_registry.c
    ../services/microkernel/v5_native_status_api.c
)

target_include_directories(v5_microkernel_contract PUBLIC
    "${V5_ROOT_DIR}/services/microkernel"
)

add_executable(v5_microkernel_manifest_smoke
    ../services/microkernel/v5_microkernel_manifest_smoke.c
)

target_link_libraries(v5_microkernel_manifest_smoke PRIVATE v5_microkernel_contract)

add_library(v5_program_runtime STATIC
    ../services/program_runtime/v5_program_preview.c
    ../services/program_runtime/v5_program_runtime.c
    ../services/program_runtime/v5_sha256.c
)

target_include_directories(v5_program_runtime PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${V5_ROOT_DIR}/services/program_runtime"
    "${V5_ROOT_DIR}/services/command_gate"
)
if(NOT WIN32)
    target_link_libraries(v5_program_runtime PUBLIC m)
endif()

add_library(v5_program_commands STATIC
    src/v5_command_first_point.c
    src/v5_command_motion.c
    src/v5_command_override.c
    src/v5_command_program.c
    src/v5_command_rtcp.c
    src/v5_command_safety.c
    src/v5_command_start.c
    src/v5_command_wcs.c
)

target_include_directories(v5_program_commands PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${V5_ROOT_DIR}/services/program_runtime"
    "${V5_ROOT_DIR}/services/command_gate"
)

target_link_libraries(v5_program_commands PUBLIC
    v5_program_runtime
    v5_command_gate_client
)

if(UNIX)
add_executable(v5_main_page_actions_smoke
    src/lv_font_zh_16.c
    src/v5_layout_icon_masks.c
    src/v5_layout_icons.c
    src/v5_main_page_actions_smoke.c
    src/v5_lvgl_headless.c
    src/v5_ui_first_frame_guard.c
    src/v5_ui_first_frame_guard_test_remote.c
    src/v5_main_page.c
    src/v5_main_page_status_text.c
    src/v5_main_page_toolpath_primitives.c
    src/v5_main_page_toolpath_geometry.c
    src/v5_main_page_toolpath_program.c
    src/v5_main_page_toolpath_raster.c
    src/v5_main_page_program_preview.c
    src/v5_main_page_input.c
    src/v5_main_page_override.c
    src/v5_main_page_action_guard.c
    src/v5_main_page_button_state.c
    src/v5_main_page_view.c
    src/v5_main_page_program_state.c
    src/v5_main_page_action_execute.c
    src/v5_main_page_home_transaction.c
    src/v5_remote_metrics.c
    src/v5_settings_page.c
    src/v5_settings_page_support.c
    src/v5_settings_page_status.c
    src/v5_settings_page_actions.c
    src/v5_settings_page_widgets.c
    src/v5_boot_closure.c
    src/v5_runtime_registry.c
    src/v5_settings_axis_table.c
    src/v5_settings_axis_table_resident.c
    src/v5_settings_axis_table_runtime.c
    src/v5_settings_axis_table_readback.c
    src/v5_settings_axis_table_widgets.c
    src/v5_settings_axis_table_commit.c
    src/v5_settings_axis_table_options.c
    src/v5_settings_axis_table_dropdown.c
    src/v5_settings_axis_table_keyboard.c
    src/v5_settings_axis_table_layout.c
    ../services/drive_profile/v5_drive_profile_snapshot.c
    src/v5_settings_actions.c
    src/v5_settings_action_ipc.c
    src/v5_main_page_actions.c
    src/v5_coordinate_panel.c
    src/v5_coordinate_digits.c
    src/v5_ui_status_view.c
    src/v5_status_shm_reader.c
)

target_include_directories(v5_main_page_actions_smoke PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${V5_ROOT_DIR}/services/command_gate"
    "${V5_ROOT_DIR}/services/program_runtime"
    "${V5_ROOT_DIR}/services/drive_profile"
    "${V5_ROOT_DIR}/services/microkernel"
    "${V5_ROOT_DIR}/services/state_publisher"
)

target_compile_definitions(v5_main_page_actions_smoke PRIVATE
    LV_CONF_INCLUDE_SIMPLE
    LV_LVGL_H_INCLUDE_SIMPLE
    V5_MAIN_PAGE_HOME_TRANSACTION_TEST_HOOKS
)

target_link_libraries(v5_main_page_actions_smoke PRIVATE lvgl v5_button_visuals v5_lvgl_clock v5_status_shm_projection v5_program_commands v5_command_gate_core v5_microkernel_contract Threads::Threads m)

add_executable(v5_main_page_smoke
    src/lv_font_zh_16.c
    src/v5_layout_icon_masks.c
    src/v5_layout_icons.c
    src/v5_main_page_smoke.c
    src/v5_lvgl_headless.c
    src/v5_ui_first_frame_guard.c
    src/v5_ui_first_frame_guard_test_remote.c
    src/v5_main_page.c
    src/v5_main_page_status_text.c
    src/v5_main_page_toolpath_primitives.c
    src/v5_main_page_toolpath_geometry.c
    src/v5_main_page_toolpath_program.c
    src/v5_main_page_toolpath_raster.c
    src/v5_main_page_program_preview.c
    src/v5_main_page_input.c
    src/v5_main_page_override.c
    src/v5_main_page_action_guard.c
    src/v5_main_page_button_state.c
    src/v5_main_page_view.c
    src/v5_main_page_program_state.c
    src/v5_main_page_action_execute.c
    src/v5_main_page_home_transaction.c
    src/v5_remote_metrics.c
    src/v5_settings_page.c
    src/v5_settings_page_support.c
    src/v5_settings_page_status.c
    src/v5_settings_page_actions.c
    src/v5_settings_page_widgets.c
    src/v5_boot_closure.c
    src/v5_runtime_registry.c
    src/v5_settings_axis_table.c
    src/v5_settings_axis_table_resident.c
    src/v5_settings_axis_table_runtime.c
    src/v5_settings_axis_table_readback.c
    src/v5_settings_axis_table_widgets.c
    src/v5_settings_axis_table_commit.c
    src/v5_settings_axis_table_options.c
    src/v5_settings_axis_table_dropdown.c
    src/v5_settings_axis_table_keyboard.c
    src/v5_settings_axis_table_layout.c
    ../services/drive_profile/v5_drive_profile_snapshot.c
    src/v5_settings_actions.c
    src/v5_settings_action_ipc.c
    src/v5_main_page_actions.c
    src/v5_coordinate_panel.c
    src/v5_coordinate_digits.c
    src/v5_ui_status_view.c
    src/v5_status_shm_reader.c
)

target_include_directories(v5_main_page_smoke PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${V5_ROOT_DIR}/services/command_gate"
    "${V5_ROOT_DIR}/services/program_runtime"
    "${V5_ROOT_DIR}/services/drive_profile"
    "${V5_ROOT_DIR}/services/microkernel"
    "${V5_ROOT_DIR}/services/state_publisher"
)

target_compile_definitions(v5_main_page_smoke PRIVATE
    LV_CONF_INCLUDE_SIMPLE
    LV_LVGL_H_INCLUDE_SIMPLE
)

target_link_libraries(v5_main_page_smoke PRIVATE lvgl v5_button_visuals v5_lvgl_clock v5_status_shm_projection v5_program_commands v5_command_gate_core v5_microkernel_contract Threads::Threads m)
endif()

add_executable(v5_ui_display_models_smoke
    src/v5_ui_display_models_smoke.c
    src/v5_coordinate_panel.c
    src/v5_remote_metrics.c
    src/v5_ui_status_view.c
    src/v5_status_shm_reader.c
)

target_include_directories(v5_ui_display_models_smoke PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

if(NOT WIN32)
    target_link_libraries(v5_ui_display_models_smoke PRIVATE m)
endif()

if(UNIX)
add_executable(v5_ui_shm_refresh_smoke
    src/v5_ui_shm_refresh_smoke.c
    src/v5_ui_model.c
    src/v5_ui_status_view.c
)

target_include_directories(v5_ui_shm_refresh_smoke PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${V5_ROOT_DIR}/services/state_publisher"
)

target_link_libraries(v5_ui_shm_refresh_smoke PRIVATE v5_status_shm_projection)
target_link_libraries(v5_ui_shm_refresh_smoke PRIVATE m)
endif()

add_executable(v5_ui_shell_refresh_smoke
    src/v5_ui_shell_refresh_smoke.c
)
target_include_directories(v5_ui_shell_refresh_smoke PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${V5_ROOT_DIR}/services/command_gate"
)

add_executable(v5_drive_write_window_smoke
    ../services/command_gate/v5_drive_write_window.c
    ../services/command_gate/v5_drive_write_window_smoke.c
)

target_link_libraries(v5_drive_write_window_smoke PRIVATE Threads::Threads)

add_executable(v5_estop_clean_state_smoke
    ../services/command_gate/v5_estop_clean_state.c
    ../services/command_gate/v5_estop_clean_state_smoke.c
)

target_link_libraries(v5_estop_clean_state_smoke PRIVATE Threads::Threads)

add_executable(v5_program_command_smoke
    src/v5_program_command_smoke.c
)

target_link_libraries(v5_program_command_smoke PRIVATE
    v5_program_commands
    v5_linuxcncrsh_format
)

if(UNIX)
add_executable(v5_command_gate_smoke
    ../services/command_gate/v5_command_gate_smoke.c
)

target_link_libraries(v5_command_gate_smoke PRIVATE v5_command_gate_core)

add_executable(v5_command_gate_validator_smoke
    ../services/command_gate/v5_command_gate_validator.c
    ../services/command_gate/v5_command_gate_validator_smoke.c
)

target_link_libraries(v5_command_gate_validator_smoke PRIVATE v5_command_gate_client)
if(NOT MSVC)
    target_link_libraries(v5_command_gate_validator_smoke PRIVATE m)
endif()

add_executable(v5_native_motion_parameters_smoke
    ../services/command_gate/v5_native_motion_parameters_smoke.c
)

target_link_libraries(v5_native_motion_parameters_smoke PRIVATE v5_command_gate_core m)

add_executable(v5_linuxcncrsh_probe
    ../services/command_gate/v5_linuxcncrsh_probe.c
)

target_link_libraries(v5_linuxcncrsh_probe PRIVATE v5_command_gate_core)

if(UNIX)
    add_executable(v5_backend_readiness_probe
        ../services/command_gate/v5_backend_readiness_probe.c
    )
    target_include_directories(v5_backend_readiness_probe PRIVATE
        "${V5_ROOT_DIR}/../linuxcnc/src/hal/user_comps"
    )
endif()

add_executable(v5_command_gate_server
    ../services/command_gate/v5_command_gate_server.c
    ../services/command_gate/v5_command_gate_server_runtime.c
    ../services/command_gate/v5_command_gate_server_execute.c
    ../services/command_gate/v5_command_gate_server_dispatch.c
    ../services/command_gate/v5_command_gate_server_io.c
    ../services/command_gate/v5_command_gate_server_response.c
    ../services/command_gate/v5_command_gate_server_transport.c
    ../services/command_gate/v5_estop_clean_state.c
    ../services/command_gate/v5_drive_write_window.c
    src/v5_process_residency.c
)

target_link_libraries(v5_command_gate_server PRIVATE v5_command_gate_core Threads::Threads)

if(UNIX)
    add_executable(v5_native_axis_zero_live_smoke
        ../services/command_gate/v5_native_axis_zero_live.c
        ../services/command_gate/v5_native_axis_zero_live_smoke.c
    )
    target_include_directories(v5_native_axis_zero_live_smoke PRIVATE
        "${V5_ROOT_DIR}/services/command_gate"
        "${V5_ROOT_DIR}/../linuxcnc/src/hal/user_comps"
    )
    target_link_libraries(v5_native_axis_zero_live_smoke PRIVATE m)
endif()

add_executable(v5_command_gate_drive_window
    ../services/command_gate/v5_command_gate_drive_window.c
)

target_link_libraries(v5_command_gate_drive_window PRIVATE v5_command_gate_client)

add_executable(v5_linuxcncrsh_golden_run
    ../services/command_gate/v5_linuxcncrsh_golden_run.c
)

target_link_libraries(v5_linuxcncrsh_golden_run PRIVATE v5_command_gate_core)

add_executable(v5_program_runtime_smoke
    ../services/program_runtime/v5_program_runtime_smoke.c
)

target_link_libraries(v5_program_runtime_smoke PRIVATE
    v5_program_runtime
    v5_command_gate_client
)

add_executable(v5_command_wrappers_smoke
    src/v5_command_wrappers_smoke.c
)

add_executable(v5_native_rtcp_status_smoke
    ../services/command_gate/v5_native_rtcp_status_smoke.c
)

target_link_libraries(v5_native_rtcp_status_smoke PRIVATE v5_command_gate_core)

add_executable(v5_native_wcs_status_smoke
    ../services/command_gate/v5_native_wcs_status_smoke.c
)

target_link_libraries(v5_native_wcs_status_smoke PRIVATE v5_command_gate_core)

add_executable(v5_native_g53_geometry_status_smoke
    ../services/command_gate/v5_native_g53_geometry_status_smoke.c
)

target_link_libraries(v5_native_g53_geometry_status_smoke PRIVATE v5_command_gate_core m)

add_executable(v5_native_modal_tool_status_smoke
    ../services/command_gate/v5_native_modal_tool_status_smoke.c
)

target_link_libraries(v5_native_modal_tool_status_smoke PRIVATE v5_command_gate_core m)

add_executable(v5_native_work_zero_smoke
    ../services/command_gate/v5_native_work_zero_smoke.c
)

target_link_libraries(v5_native_work_zero_smoke PRIVATE v5_command_gate_core m)
endif()

add_executable(v5_native_operator_error_status_smoke
    ../services/state_publisher/v5_native_operator_error_status_smoke.c
)

target_link_libraries(v5_native_operator_error_status_smoke PRIVATE v5_operator_error_status)

if(UNIX)
target_link_libraries(v5_command_wrappers_smoke PRIVATE
    v5_program_commands
    v5_command_gate_core
)

add_executable(v5_parameter_table_smoke
    src/v5_parameter_table_smoke.c
)

target_link_libraries(v5_parameter_table_smoke PRIVATE
    v5_command_gate_core
)

add_executable(v5_settings_axis_table_smoke
    src/lv_font_zh_16.c
    src/v5_settings_axis_table_smoke.c
    src/v5_ui_first_frame_guard.c
    src/v5_ui_first_frame_guard_test_remote.c
    src/v5_boot_closure.c
    src/v5_runtime_registry.c
    src/v5_settings_axis_table.c
    src/v5_settings_axis_table_resident.c
    src/v5_settings_axis_table_runtime.c
    src/v5_settings_axis_table_readback.c
    src/v5_settings_axis_table_widgets.c
    src/v5_settings_axis_table_commit.c
    src/v5_settings_axis_table_options.c
    src/v5_settings_axis_table_dropdown.c
    src/v5_settings_axis_table_keyboard.c
    src/v5_settings_axis_table_layout.c
    ../services/drive_profile/v5_drive_profile_snapshot.c
)

target_include_directories(v5_settings_axis_table_smoke PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${V5_ROOT_DIR}/services/command_gate"
    "${V5_ROOT_DIR}/services/drive_profile"
    "${V5_ROOT_DIR}/services/microkernel"
)

target_compile_definitions(v5_settings_axis_table_smoke PRIVATE
    LV_CONF_INCLUDE_SIMPLE
    LV_LVGL_H_INCLUDE_SIMPLE
)

target_link_libraries(v5_settings_axis_table_smoke PRIVATE
    lvgl
    v5_button_visuals
    v5_command_gate_core
    v5_microkernel_contract
)

target_include_directories(v5_program_page_double_click_smoke PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${V5_ROOT_DIR}/services/command_gate"
    "${V5_ROOT_DIR}/services/drive_profile"
    "${V5_ROOT_DIR}/services/microkernel"
    "${V5_ROOT_DIR}/services/state_publisher"
)

target_compile_definitions(v5_program_page_double_click_smoke PRIVATE
    LV_CONF_INCLUDE_SIMPLE
    LV_LVGL_H_INCLUDE_SIMPLE
    _CRT_SECURE_NO_WARNINGS
    V5_UI_SHELL_TEST_HOOKS
)

target_link_libraries(v5_program_page_double_click_smoke PRIVATE lvgl v5_button_visuals v5_lvgl_clock v5_status_shm_projection v5_microkernel_contract v5_program_commands v5_command_gate_core Threads::Threads m)

target_include_directories(v5_lvgl_shell PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${V5_ROOT_DIR}/services/command_gate"
    "${V5_ROOT_DIR}/services/drive_profile"
    "${V5_ROOT_DIR}/services/microkernel"
    "${V5_ROOT_DIR}/services/state_publisher"
)

target_compile_definitions(v5_lvgl_shell PRIVATE
    LV_CONF_INCLUDE_SIMPLE
    LV_LVGL_H_INCLUDE_SIMPLE
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(v5_lvgl_shell PRIVATE lvgl v5_button_visuals v5_lvgl_clock v5_status_shm_projection v5_microkernel_contract v5_program_commands v5_command_gate_client Threads::Threads m)
endif()

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    set(V5_LOCAL_REGRESSION_TARGETS)

    macro(v5_register_local_smoke V5_TEST_TARGET)
        if(NOT TARGET "${V5_TEST_TARGET}")
            message(FATAL_ERROR "Local regression target is missing: ${V5_TEST_TARGET}")
        endif()
        add_test(NAME "${V5_TEST_TARGET}" COMMAND $<TARGET_FILE:${V5_TEST_TARGET}>)
        set_tests_properties("${V5_TEST_TARGET}" PROPERTIES
            WORKING_DIRECTORY "${V5_ROOT_DIR}/.."
            LABELS "v5_local;${ARGN}"
            TIMEOUT 30
        )
        list(APPEND V5_LOCAL_REGRESSION_TARGETS "${V5_TEST_TARGET}")
    endmacro()

    v5_register_local_smoke(v5_microkernel_manifest_smoke architecture_contract lifeline_01_startup)
    v5_register_local_smoke(v5_position_status_publisher_smoke state_projection lifeline_01_startup lifeline_07_model_rtcp lifeline_08_axis_drive)
    v5_register_local_smoke(v5_state_publisher_cadence_smoke state_projection)
    v5_register_local_smoke(v5_program_scene_producer_smoke state_projection lifeline_05_program lifeline_07_model_rtcp)
    v5_register_local_smoke(v5_coordinate_digits_smoke ui lifeline_10_ui_remote)
    v5_register_local_smoke(v5_lvgl_remote_display_delta_smoke ui lifeline_10_ui_remote)
    v5_register_local_smoke(v5_ui_display_models_smoke ui state_projection lifeline_07_model_rtcp lifeline_10_ui_remote)
    v5_register_local_smoke(v5_ui_shell_operator_error_smoke ui lifeline_02_safety lifeline_10_ui_remote)
    v5_register_local_smoke(v5_ui_shell_refresh_smoke ui lifeline_10_ui_remote)
    v5_register_local_smoke(v5_ui_first_frame_guard_smoke ui lifeline_01_startup lifeline_10_ui_remote)
    v5_register_local_smoke(v5_ui_page_cache_registry_smoke ui lifeline_10_ui_remote)
    v5_register_local_smoke(v5_native_operator_error_status_smoke state_projection lifeline_02_safety lifeline_10_ui_remote)
    v5_register_local_smoke(v5_drive_write_window_smoke control lifeline_02_safety lifeline_08_axis_drive)
    v5_register_local_smoke(v5_estop_clean_state_smoke control lifeline_02_safety)
    v5_register_local_smoke(v5_program_command_smoke control lifeline_05_program)

    if(UNIX)
        v5_register_local_smoke(v5_remote_ui_native_packer_smoke ui lifeline_10_ui_remote)
        v5_register_local_smoke(v5_program_page_double_click_smoke ui lifeline_05_program lifeline_10_ui_remote)
        v5_register_local_smoke(v5_ui_shell_program_delete_smoke ui lifeline_05_program lifeline_10_ui_remote)
        v5_register_local_smoke(v5_cpu_usage_snapshot_smoke state_projection)
        v5_register_local_smoke(v5_state_publisher_smoke state_projection lifeline_01_startup lifeline_07_model_rtcp lifeline_10_ui_remote)
        v5_register_local_smoke(v5_main_page_actions_smoke ui control lifeline_02_safety lifeline_03_home lifeline_04_jog lifeline_05_program lifeline_06_coordinates lifeline_07_model_rtcp lifeline_10_ui_remote)
        v5_register_local_smoke(v5_main_page_smoke ui lifeline_10_ui_remote)
        v5_register_local_smoke(v5_ui_shm_refresh_smoke ui state_projection lifeline_01_startup lifeline_10_ui_remote)
        v5_register_local_smoke(v5_command_gate_smoke control lifeline_02_safety lifeline_03_home lifeline_04_jog lifeline_05_program lifeline_06_coordinates lifeline_07_model_rtcp)
        v5_register_local_smoke(v5_command_gate_validator_smoke control architecture_contract)
        v5_register_local_smoke(v5_native_motion_parameters_smoke control lifeline_03_home lifeline_04_jog lifeline_07_model_rtcp lifeline_08_axis_drive)
        v5_register_local_smoke(v5_native_axis_zero_live_smoke control lifeline_08_axis_drive)
        v5_register_local_smoke(v5_program_runtime_smoke control lifeline_05_program)
        v5_register_local_smoke(v5_command_wrappers_smoke control lifeline_02_safety lifeline_03_home lifeline_04_jog lifeline_05_program lifeline_06_coordinates lifeline_07_model_rtcp)
        v5_register_local_smoke(v5_native_rtcp_status_smoke control lifeline_07_model_rtcp)
        v5_register_local_smoke(v5_native_wcs_status_smoke control lifeline_06_coordinates)
        v5_register_local_smoke(v5_native_g53_geometry_status_smoke control lifeline_06_coordinates)
        v5_register_local_smoke(v5_native_modal_tool_status_smoke control lifeline_05_program)
        v5_register_local_smoke(v5_native_work_zero_smoke control lifeline_06_coordinates)
        v5_register_local_smoke(v5_parameter_table_smoke settings lifeline_08_axis_drive lifeline_09_restart)
        v5_register_local_smoke(v5_settings_axis_table_smoke ui settings lifeline_08_axis_drive lifeline_09_restart)
    endif()

    add_custom_target(v5_local_regression
        COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure -L "^v5_local$" -C "$<CONFIG>"
        DEPENDS ${V5_LOCAL_REGRESSION_TARGETS}
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        COMMENT "Build and run the platform-local V5 smoke regression set"
        USES_TERMINAL
    )
endif()

if(UNIX)
# The deploy manifest is the single product-runtime inventory.  Derive the
# aggregate ARM build target from every board/app binary row so build, deploy
# and SD-card assembly cannot maintain separate executable lists. Recipe-owned
# binaries (currently EtherCAT lcec.so) remain in the same deploy inventory but
# are built by their canonical PetaLinux recipe rather than this CMake project.
set(V5_RUNTIME_DEPLOY_MANIFEST
    "${CMAKE_CURRENT_SOURCE_DIR}/../config/deploy/v5_runtime_deploy_manifest.tsv"
)
if(NOT EXISTS "${V5_RUNTIME_DEPLOY_MANIFEST}")
    message(FATAL_ERROR "V5 runtime deploy manifest is missing: ${V5_RUNTIME_DEPLOY_MANIFEST}")
endif()

file(STRINGS "${V5_RUNTIME_DEPLOY_MANIFEST}" V5_RUNTIME_DEPLOY_ROWS)
set(V5_PRODUCT_RUNTIME_TARGETS)
set(V5_UNIX_ONLY_RUNTIME_TARGETS
    v5_backend_readiness_probe
    v5_position_status_publisher
    v5_remote_ui_packer
)
foreach(V5_RUNTIME_DEPLOY_ROW IN LISTS V5_RUNTIME_DEPLOY_ROWS)
    string(STRIP "${V5_RUNTIME_DEPLOY_ROW}" V5_RUNTIME_DEPLOY_ROW_STRIPPED)
    if(V5_RUNTIME_DEPLOY_ROW_STRIPPED STREQUAL "" OR
       V5_RUNTIME_DEPLOY_ROW_STRIPPED MATCHES "^#")
        continue()
    endif()
    string(REPLACE "\t" ";" V5_RUNTIME_DEPLOY_FIELDS "${V5_RUNTIME_DEPLOY_ROW}")
    list(LENGTH V5_RUNTIME_DEPLOY_FIELDS V5_RUNTIME_DEPLOY_FIELD_COUNT)
    if(NOT V5_RUNTIME_DEPLOY_FIELD_COUNT EQUAL 4)
        message(FATAL_ERROR "Invalid V5 runtime deploy row: ${V5_RUNTIME_DEPLOY_ROW}")
    endif()
    list(GET V5_RUNTIME_DEPLOY_FIELDS 0 V5_RUNTIME_DEPLOY_KIND)
    if(V5_RUNTIME_DEPLOY_KIND STREQUAL "binary")
        list(GET V5_RUNTIME_DEPLOY_FIELDS 1 V5_RUNTIME_DEPLOY_SOURCE)
        if(V5_RUNTIME_DEPLOY_SOURCE MATCHES "^build/board/app/")
            get_filename_component(V5_RUNTIME_DEPLOY_TARGET "${V5_RUNTIME_DEPLOY_SOURCE}" NAME)
            if(NOT TARGET "${V5_RUNTIME_DEPLOY_TARGET}")
                if(NOT UNIX AND V5_RUNTIME_DEPLOY_TARGET IN_LIST V5_UNIX_ONLY_RUNTIME_TARGETS)
                    continue()
                else()
                    message(FATAL_ERROR
                        "Deploy manifest board/app binary has no CMake target: ${V5_RUNTIME_DEPLOY_TARGET}"
                    )
                endif()
            endif()
            list(APPEND V5_PRODUCT_RUNTIME_TARGETS "${V5_RUNTIME_DEPLOY_TARGET}")
        elseif(NOT V5_RUNTIME_DEPLOY_SOURCE STREQUAL "build/ethercat/lcec.so")
            message(FATAL_ERROR
                "Deploy manifest binary has no registered build owner: ${V5_RUNTIME_DEPLOY_SOURCE}"
            )
        endif()
    endif()
endforeach()

list(REMOVE_DUPLICATES V5_PRODUCT_RUNTIME_TARGETS)
if(NOT V5_PRODUCT_RUNTIME_TARGETS)
    message(FATAL_ERROR "V5 runtime deploy manifest has no binary targets")
endif()

add_custom_target(v5_product_runtime DEPENDS ${V5_PRODUCT_RUNTIME_TARGETS})
endif()
