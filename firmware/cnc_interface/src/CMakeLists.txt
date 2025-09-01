# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)
#list(ADD_ITEM app_srcs
#    "${CMAKE_SOURCE_DIR}/lvgl_ui/data_binding.c",
#    "${CMAKE_SOURCE_DIR}/lvgl_ui/data_binding.c",
#    "${CMAKE_SOURCE_DIR}/lvgl_ui/api_spec.c",
#    "${CMAKE_SOURCE_DIR}/lvgl_ui/lu_utils.c",
#    "${CMAKE_SOURCE_DIR}/lvgl_ui/debug_log.c",
#)


list(REMOVE_ITEM app_srcs
    "${CMAKE_SOURCE_DIR}/src/**",
    "${CMAKE_SOURCE_DIR}/lvgl_ui/ir_debug_printer.c",
    "${CMAKE_SOURCE_DIR}/lvgl_ui/generator.c",
    "${CMAKE_SOURCE_DIR}/lvgl_ui/c_code_printer.c",
    "${CMAKE_SOURCE_DIR}/lvgl_ui/debug_log.c",
    "${CMAKE_SOURCE_DIR}/lvgl_ui/ir.c",
    "${CMAKE_SOURCE_DIR}/lvgl_ui/ir_debug_printer.c",
    "${CMAKE_SOURCE_DIR}/lvgl_ui/ir_printer.c",
    "${CMAKE_SOURCE_DIR}/lvgl_ui/lvgl_renderer.c",
    "${CMAKE_SOURCE_DIR}/lvgl_ui/registry.c",
    "${CMAKE_SOURCE_DIR}/lvgl_ui/yaml_parser.c",
    "${CMAKE_SOURCE_DIR}/lvgl_ui/ui_sim.c",
    "${CMAKE_SOURCE_DIR}/lvgl_ui/warning_printer.c",
    "${CMAKE_SOURCE_DIR}/lvgl_ui/viewer/sdl_viewer.c",
    "${CMAKE_SOURCE_DIR}/lvgl_ui/viewer/lvgl_assert_handler.c",
    "${CMAKE_SOURCE_DIR}/lvgl_ui/viewer/view_inspector.c",
)

# target_add_binary_data(app_sources "managed_components/espressif__esp_insights/server_certs/https_server.crt" BINARY)

idf_component_register(SRCS ${app_sources})


