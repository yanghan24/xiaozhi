set(CONTROL_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../main/device_control")
idf_component_register(SRCS "identity_loss_main.cc"
    "${CONTROL_DIR}/device_identity.cc"
    "${CONTROL_DIR}/nvs_erase_guard.cc"
    "${CONTROL_DIR}/identity_config.cc"
    "${CONTROL_DIR}/control_model.cc"
    INCLUDE_DIRS "${CONTROL_DIR}"
    PRIV_REQUIRES nvs_flash json esp_driver_uart esp_hw_support)
target_link_options(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=nvs_flash_erase" "-Wl,--wrap=nvs_flash_erase_partition")
