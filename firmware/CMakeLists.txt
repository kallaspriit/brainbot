cmake_minimum_required(VERSION 3.16)

# The board ships with ESP32-P4 silicon rev 1.3 or rev 3.x, and firmware for one does not boot on the other. Rev 3.x is
# the default; build for rev 1.3 with -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.rev1.defaults" (see README).
if(NOT DEFINED SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.rev3.defaults")
endif()

# Keep the generated sdkconfig inside the build directory, so each revision's build directory has its own
set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(sparky)
