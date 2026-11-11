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

add_executable(control_worker_tests control_worker_test.cc
  "${CONTROL_DIR}/device_control.cc" "${CONTROL_DIR}/control_model.cc"
  "${IDF_PATH}/components/json/cJSON/cJSON.c")
target_include_directories(control_worker_tests PRIVATE stubs "${CONTROL_DIR}"
  "${IDF_PATH}/components/json/cJSON")
target_compile_options(control_worker_tests PRIVATE -Wall -Wextra -Werror)
foreach(operation account unbind)
  foreach(outcome unauthorized forbidden tls invalid missing server network timeout)
    add_test(NAME control_worker_${operation}_${outcome}
      COMMAND control_worker_tests ${operation} ${outcome})
  endforeach()
endforeach()
add_test(NAME control_worker_account_nullable COMMAND control_worker_tests account nullable)
foreach(outcome success signed-out reauth auth-pending)
  add_test(NAME control_worker_unbind_${outcome} COMMAND control_worker_tests unbind ${outcome})
endforeach()

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

add_executable(nvs_erase_guard_tests nvs_erase_guard_test.cc stubs/nvs_flash.cc
  "${CONTROL_DIR}/nvs_erase_guard.cc")
target_include_directories(nvs_erase_guard_tests PRIVATE stubs)
target_compile_options(nvs_erase_guard_tests PRIVATE -Wall -Wextra -Werror)
target_link_options(nvs_erase_guard_tests PRIVATE
  -Wl,--wrap=nvs_flash_erase -Wl,--wrap=nvs_flash_erase_partition)
add_test(NAME nvs_erase_guard_tests COMMAND nvs_erase_guard_tests)

add_executable(device_delivery_tests delivery_test.cc
  "${CONTROL_DIR}/delivery_model.cc" "${IDF_PATH}/components/json/cJSON/cJSON.c")
target_include_directories(device_delivery_tests PRIVATE "${CONTROL_DIR}"
  "${IDF_PATH}/components/json/cJSON")
target_compile_options(device_delivery_tests PRIVATE -Wall -Wextra -Werror)
add_test(NAME device_delivery_tests COMMAND device_delivery_tests)

# Exercise the installed ESP-TLS state machine with deterministic socket timing.
# Keep SDK code in the build directory and regenerate it when that SDK changes.
set(SDK_TLS_SOURCE "${IDF_PATH}/components/esp-tls/esp_tls.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SDK_TLS_SOURCE}")
file(READ "${SDK_TLS_SOURCE}" SDK_TLS_TEXT)
string(FIND "${SDK_TLS_TEXT}" "static int esp_tls_low_level_conn(" SDK_TLS_BEGIN)
if(SDK_TLS_BEGIN LESS 0)
  message(FATAL_ERROR "Installed ESP-IDF does not expose the expected TLS connection source")
endif()
string(SUBSTRING "${SDK_TLS_TEXT}" ${SDK_TLS_BEGIN} -1 SDK_TLS_TAIL)
string(FIND "${SDK_TLS_TAIL}" "\n/**" SDK_TLS_END)
if(SDK_TLS_END LESS 0)
  message(FATAL_ERROR "Cannot locate the end of the installed ESP-TLS connection function")
endif()
string(SUBSTRING "${SDK_TLS_TAIL}" 0 ${SDK_TLS_END} SDK_TLS_FUNCTION)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/sdk_tls_connection.inc" "${SDK_TLS_FUNCTION}\n")
add_executable(control_tls_tests control_tls_test.cc "${CONTROL_DIR}/control_tls.cc")
target_include_directories(control_tls_tests PRIVATE stubs "${CONTROL_DIR}"
  "${CMAKE_CURRENT_BINARY_DIR}")
target_link_options(control_tls_tests PRIVATE -Wl,--wrap=select -Wl,--wrap=getsockopt)
target_compile_options(control_tls_tests PRIVATE -Wall -Wextra -Werror)
add_test(NAME control_tls_tests COMMAND control_tls_tests)
