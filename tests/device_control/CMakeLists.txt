cmake_minimum_required(VERSION 3.16)
project(device_control_host_tests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(IDF_PATH "$ENV{IDF_PATH}" CACHE PATH "ESP-IDF source tree for its portable JSON and HTTP parser")
if(NOT EXISTS "${IDF_PATH}/components/json/cJSON/cJSON.c")
  message(FATAL_ERROR "Set IDF_PATH to an ESP-IDF source tree")
endif()
set(CONTROL_DIR "${CMAKE_CURRENT_LIST_DIR}/../../main/device_control")
add_executable(device_control_tests control_test.cc
  "${CONTROL_DIR}/control_model.cc"
  "${CONTROL_DIR}/control_http.cc"
  "${IDF_PATH}/components/json/cJSON/cJSON.c"
  "${IDF_PATH}/components/http_parser/http_parser.c")
target_include_directories(device_control_tests PRIVATE "${CONTROL_DIR}" "${IDF_PATH}/components/json/cJSON" "${IDF_PATH}/components/http_parser")
target_compile_options(device_control_tests PRIVATE -Wall -Wextra -Werror)
enable_testing()
add_test(NAME device_control_tests COMMAND device_control_tests)

find_package(Threads REQUIRED)
add_executable(controlled_audio_access_tests
  "${CMAKE_CURRENT_LIST_DIR}/../audio_access/controlled_audio_access_test.cc")
target_include_directories(controlled_audio_access_tests PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../../main/audio")
target_link_libraries(controlled_audio_access_tests PRIVATE Threads::Threads)
target_compile_options(controlled_audio_access_tests PRIVATE -Wall -Wextra -Werror)
add_test(NAME controlled_audio_access_tests COMMAND controlled_audio_access_tests)

add_executable(processing_audio_buffer_tests
  "${CMAKE_CURRENT_LIST_DIR}/../audio_access/processing_audio_buffer_test.cc")
target_include_directories(processing_audio_buffer_tests PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../../main/audio")
target_compile_options(processing_audio_buffer_tests PRIVATE -Wall -Wextra -Werror)
add_test(NAME processing_audio_buffer_tests COMMAND processing_audio_buffer_tests)

add_executable(device_identity_tests identity_test.cc
  "${CONTROL_DIR}/identity_config.cc" "${CONTROL_DIR}/device_identity.cc"
  "${CONTROL_DIR}/control_model.cc" "${IDF_PATH}/components/json/cJSON/cJSON.c")
target_include_directories(device_identity_tests PRIVATE stubs "${CONTROL_DIR}"
  "${IDF_PATH}/components/json/cJSON")
target_compile_options(device_identity_tests PRIVATE -Wall -Wextra -Werror)
add_test(NAME device_identity_tests COMMAND device_identity_tests)
