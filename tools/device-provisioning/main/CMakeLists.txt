set(CONTROL_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../main/device_control")
idf_component_register(SRCS "provision_main.cc"
    "${CONTROL_DIR}/device_provisioning.cc"
    "${CONTROL_DIR}/device_identity.cc"
    "${CONTROL_DIR}/identity_config.cc"
    "${CONTROL_DIR}/control_model.cc"
    INCLUDE_DIRS "${CONTROL_DIR}"
    PRIV_REQUIRES nvs_flash json esp_driver_usb_serial_jtag esp_hw_support)
