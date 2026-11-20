cmake_minimum_required(VERSION 3.16)
project(identity_loss_guard_tests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(IDF_PATH "$ENV{IDF_PATH}" CACHE PATH "ESP-IDF source tree")
set(CONTROL_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../main/device_control")
add_executable(identity_loss_guard_tests guard_test.cc
    "${CONTROL_DIR}/device_identity.cc" "${CONTROL_DIR}/identity_config.cc"
    "${CONTROL_DIR}/control_model.cc" "${IDF_PATH}/components/json/cJSON/cJSON.c")
target_include_directories(identity_loss_guard_tests PRIVATE stubs "${CONTROL_DIR}"
    "${IDF_PATH}/components/json/cJSON")
target_compile_options(identity_loss_guard_tests PRIVATE -Wall -Wextra -Werror)
enable_testing()
add_test(NAME identity_loss_guard_tests COMMAND identity_loss_guard_tests)
