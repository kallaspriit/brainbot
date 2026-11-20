# Every source under main/, including the generated fonts, so moved or added files need no edits here
file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.c")

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS "."
    REQUIRES esp32_p4_wifi6_touch_lcd_7b espressif__esp_lcd_ek79007 esp_lcd esp_hw_support lvgl__lvgl esp_driver_twai esp_driver_uart esp_driver_gpio esp_adc esp_timer esp_app_format bt nvs_flash espressif__esp_hosted espressif__esp_video)

# The commit for the version on the splash (BuildInfo.cpp only, so a new commit doesn't rebuild everything). CMake reads
# it again after each commit or checkout; a build from uncommitted changes shows the last commit
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE BUILD_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)

if(NOT BUILD_COMMIT)
    set(BUILD_COMMIT "unknown")
endif()

set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/BuildInfo.cpp" PROPERTIES COMPILE_DEFINITIONS "BUILD_COMMIT=\"${BUILD_COMMIT}\"")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../../.git/logs/HEAD")
