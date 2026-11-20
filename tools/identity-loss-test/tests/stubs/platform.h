#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
using esp_err_t = int;
constexpr int ESP_OK=0, ESP_FAIL=1, ESP_ERR_INVALID_SIZE=2, ESP_ERR_INVALID_STATE=3;
constexpr int ESP_ERR_INVALID_ARG=4, ESP_ERR_NVS_NOT_FOUND=5, ESP_ERR_NVS_TYPE_MISMATCH=6;
#define ESP_ERROR_CHECK(x) assert((x)==ESP_OK)
using nvs_handle_t=unsigned;
enum nvs_open_mode_t {NVS_READONLY,NVS_READWRITE};
enum nvs_type_t {NVS_TYPE_ANY,NVS_TYPE_I8,NVS_TYPE_U8,NVS_TYPE_I16,NVS_TYPE_U16,
                NVS_TYPE_I32,NVS_TYPE_U32,NVS_TYPE_I64,NVS_TYPE_U64,NVS_TYPE_STR,NVS_TYPE_BLOB};
struct nvs_entry_info_t {char namespace_name[16];char key[16];nvs_type_t type;};
struct TestIterator;
using nvs_iterator_t=TestIterator*;
esp_err_t nvs_open(const char*,nvs_open_mode_t,nvs_handle_t*);
void nvs_close(nvs_handle_t);
esp_err_t nvs_get_blob(nvs_handle_t,const char*,void*,size_t*);
esp_err_t nvs_set_blob(nvs_handle_t,const char*,const void*,size_t);
esp_err_t nvs_get_str(nvs_handle_t,const char*,char*,size_t*);
esp_err_t nvs_set_str(nvs_handle_t,const char*,const char*);
esp_err_t nvs_commit(nvs_handle_t);
esp_err_t nvs_erase_key(nvs_handle_t,const char*);
esp_err_t nvs_erase_all(nvs_handle_t);
esp_err_t nvs_entry_find(const char*,const char*,nvs_type_t,nvs_iterator_t*);
esp_err_t nvs_entry_next(nvs_iterator_t*);
esp_err_t nvs_entry_info(nvs_iterator_t,nvs_entry_info_t*);
void nvs_release_iterator(nvs_iterator_t);
esp_err_t nvs_flash_init();
esp_err_t esp_efuse_mac_get_default(uint8_t*);
#define NUMBER_GETTER(suffix,type) esp_err_t nvs_get_##suffix(nvs_handle_t,const char*,type*);
NUMBER_GETTER(i8,int8_t) NUMBER_GETTER(u8,uint8_t)
NUMBER_GETTER(i16,int16_t) NUMBER_GETTER(u16,uint16_t)
NUMBER_GETTER(i32,int32_t) NUMBER_GETTER(u32,uint32_t)
NUMBER_GETTER(i64,int64_t) NUMBER_GETTER(u64,uint64_t)
#undef NUMBER_GETTER
using uart_port_t=int;
struct uart_config_t {int baud_rate,data_bits,parity,stop_bits,flow_ctrl,source_clk;};
constexpr int UART_DATA_8_BITS=8,UART_PARITY_DISABLE=0,UART_STOP_BITS_1=1;
constexpr int UART_HW_FLOWCTRL_DISABLE=0,UART_SCLK_DEFAULT=0,ESP_LOG_NONE=0;
int uart_write_bytes(uart_port_t,const void*,size_t);
esp_err_t uart_wait_tx_done(uart_port_t,int);
esp_err_t uart_param_config(uart_port_t,const uart_config_t*);
bool uart_is_driver_installed(uart_port_t);
esp_err_t uart_driver_install(uart_port_t,int,int,int,void*,int);
int uart_read_bytes(uart_port_t,void*,uint32_t,int);
void esp_log_level_set(const char*,int);
#define pdMS_TO_TICKS(x) (x)
