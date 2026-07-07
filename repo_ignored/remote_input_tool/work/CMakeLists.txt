set(V5_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(V5_LVGL_DIR "${V5_ROOT_DIR}/third_party/lvgl-v8.3.11")

if(POLICY CMP0177)
    set(CMAKE_POLICY_DEFAULT_CMP0177 NEW)
endif()

set(LV_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE STRING "v5 LVGL config" FORCE)
set(LV_CONF_INCLUDE_SIMPLE ON CACHE BOOL "Use simple lv_conf include" FORCE)
set(LV_LVGL_H_INCLUDE_SIMPLE ON CACHE BOOL "Use simple LVGL include" FORCE)

add_subdirectory("${V5_LVGL_DIR}" "${CMAKE_BINARY_DIR}/third_party/lvgl" EXCLUDE_FROM_ALL)

add_executable(v5_lvgl_shell
    src/lv_font_zh_16.c
    src/v5_layout_icon_masks.c
    src/v5_layout_icons.c
    src/main.c
    src/v5_boot_closure.c
    src/v5_coordinate_panel.c
    src/v5_lvgl_headless.c
    src/v5_lvgl_remote_display.c
    src/v5_lvgl_remote_input.c
    src/v5_lvgl_touch_input.c
    src/v5_main_page.c
    src/v5_settings_page.c
    src/v5_main_page_actions.c
    src/v5_parameter_table.c
    src/v5_toolpath_display.c
    src/v5_ui_model.c
    src/v5_ui_shell.c
    src/v5_ui_status_view.c
    src/v5_runtime_registry.c
    ../services/drive_profile/v5_drive_profile_snapshot.c
)

add_library(v5_status_shm_projection STATIC
    src/v5_status_shm_reader.c
    ../services/state_publisher/v5_native_sample.c
    ../services/state_publisher/v5_state_publisher_service.c
    ../services/state_publisher/v5_status_shm_mmap.c
    ../services/state_publisher/v5_status_shm_writer.c
)

target_include_directories(v5_status_shm_projection PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${V5_ROOT_DIR}/services/state_publisher"
)

add_executable(v5_state_publisher_smoke
    ../services/state_publisher/v5_state_publisher.c
)

target_link_libraries(v5_state_publisher_smoke PRIVATE v5_status_shm_projection)

add_executable(v5_state_publisher
    ../services/state_publisher/v5_state_publisher_main.c
)

target_link_libraries(v5_state_publisher PRIVATE v5_status_shm_projection)

add_executable(v5_touch_diagnostics
    ../services/ui/v5_touch_diagnostics.c
)

add_library(v5_command_gate_core STATIC
    src/v5_parameter_table.c
    ../services/command_gate/v5_command_gate.c
    ../services/command_gate/v5_command_table.c
    ../services/command_gate/v5_linuxcncrsh_client.c
    ../services/command_gate/v5_native_readback.c
    ../services/command_gate/v5_parameter_owner_map.c
    ../services/command_gate/v5_settings_apply.c
)

target_include_directories(v5_command_gate_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${V5_ROOT_DIR}/services/command_gate"
)


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
    ../services/program_runtime/v5_program_runtime.c
)

target_include_directories(v5_program_runtime PUBLIC
    "${V5_ROOT_DIR}/services/program_runtime"
    "${V5_ROOT_DIR}/services/command_gate"
)

add_library(v5_program_commands STATIC
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
    v5_command_gate_core
)





add_executable(v5_main_page_actions_smoke
    src/lv_font_zh_16.c
    src/v5_layout_icon_masks.c
    src/v5_layout_icons.c
    src/v5_main_page_actions_smoke.c
    src/v5_lvgl_headless.c
    src/v5_main_page.c
    src/v5_settings_page.c
    src/v5_main_page_actions.c
    src/v5_coordinate_panel.c
    src/v5_toolpath_display.c
    src/v5_ui_status_view.c
    src/v5_status_shm_reader.c
)

target_include_directories(v5_main_page_actions_smoke PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${V5_ROOT_DIR}/services/command_gate"
    "${V5_ROOT_DIR}/services/program_runtime"
)

target_compile_definitions(v5_main_page_actions_smoke PRIVATE
    LV_CONF_INCLUDE_SIMPLE
    LV_LVGL_H_INCLUDE_SIMPLE
)

target_link_libraries(v5_main_page_actions_smoke PRIVATE lvgl v5_program_commands v5_command_gate_core)

add_executable(v5_main_page_smoke
    src/lv_font_zh_16.c
    src/v5_layout_icon_masks.c
    src/v5_layout_icons.c
    src/v5_main_page_smoke.c
    src/v5_lvgl_headless.c
    src/v5_main_page.c
    src/v5_settings_page.c
    src/v5_main_page_actions.c
    src/v5_coordinate_panel.c
    src/v5_toolpath_display.c
    src/v5_ui_status_view.c
    src/v5_status_shm_reader.c
)

target_include_directories(v5_main_page_smoke PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${V5_ROOT_DIR}/services/command_gate"
    "${V5_ROOT_DIR}/services/program_runtime"
)

target_compile_definitions(v5_main_page_smoke PRIVATE
    LV_CONF_INCLUDE_SIMPLE
    LV_LVGL_H_INCLUDE_SIMPLE
)

target_link_libraries(v5_main_page_smoke PRIVATE lvgl v5_program_commands v5_command_gate_core)

add_executable(v5_ui_display_models_smoke
    src/v5_ui_display_models_smoke.c
    src/v5_coordinate_panel.c
    src/v5_toolpath_display.c
    src/v5_ui_status_view.c
    src/v5_status_shm_reader.c
)

target_include_directories(v5_ui_display_models_smoke PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

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

add_executable(v5_command_gate_smoke
    ../services/command_gate/v5_command_gate_smoke.c
)

target_link_libraries(v5_command_gate_smoke PRIVATE v5_command_gate_core)

add_executable(v5_linuxcncrsh_probe
    ../services/command_gate/v5_linuxcncrsh_probe.c
)

target_link_libraries(v5_linuxcncrsh_probe PRIVATE v5_command_gate_core)

add_executable(v5_linuxcncrsh_golden_run
    ../services/command_gate/v5_linuxcncrsh_golden_run.c
)

target_link_libraries(v5_linuxcncrsh_golden_run PRIVATE v5_command_gate_core)

add_executable(v5_program_runtime_smoke
    ../services/program_runtime/v5_program_runtime_smoke.c
)

target_link_libraries(v5_program_runtime_smoke PRIVATE
    v5_program_runtime
    v5_command_gate_core
)

add_executable(v5_program_command_smoke
    src/v5_program_command_smoke.c
)

target_link_libraries(v5_program_command_smoke PRIVATE
    v5_program_commands
    v5_command_gate_core
)

add_executable(v5_command_wrappers_smoke
    src/v5_command_wrappers_smoke.c
)

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

target_link_libraries(v5_lvgl_shell PRIVATE lvgl v5_status_shm_projection v5_microkernel_contract v5_program_commands v5_command_gate_core)
