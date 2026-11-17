# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Add this line to disable the specific warning
add_compile_options(-Wno-missing-field-initializers)

# The default .com component storage endpoint can return an empty/non-JSON
# response on some mainland China networks. Use Espressif's official China
# mirror by default, while still allowing an explicit shell environment value
# to override it. This must be set before project.cmake initializes the
# component manager.
if(NOT DEFINED ENV{IDF_COMPONENT_STORAGE_URL} OR "$ENV{IDF_COMPONENT_STORAGE_URL}" STREQUAL "")
    set(ENV{IDF_COMPONENT_STORAGE_URL} "https://components-file.espressif.cn")
    message(STATUS "IDF component storage: $ENV{IDF_COMPONENT_STORAGE_URL}")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)

set(PROJECT_VER "2.2.6")
project(xiaozhi)
