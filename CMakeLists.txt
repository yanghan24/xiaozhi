# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Add this line to disable the specific warning
add_compile_options(-Wno-missing-field-initializers)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)

set(PROJECT_VER "2.6.34")
project(xiaozhi)

if(CONFIG_XIAOZHI_SPEAKER_ACCOUNT AND CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3)
    include(cmake/p4x-display-diagnostics.cmake)
endif()

if(CONFIG_XIAOZHI_SPEAKER_TOUCH_WIFI)
    include(cmake/p4x-touch-wifi.cmake)
endif()
