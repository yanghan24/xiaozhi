#pragma once
#include <cstddef>
#include <cstdint>
#include "esp_err.h"
using nvs_handle_t = unsigned;
enum nvs_open_mode_t { NVS_READONLY, NVS_READWRITE };
constexpr int NVS_TYPE_ANY = 0;
struct nvs_entry_info_t {
    char namespace_name[16];
};
struct TestIterator;
using nvs_iterator_t = TestIterator*;
esp_err_t nvs_open(const char*, nvs_open_mode_t, nvs_handle_t*);
void nvs_close(nvs_handle_t);
esp_err_t nvs_get_blob(nvs_handle_t, const char*, void*, size_t*);
esp_err_t nvs_set_blob(nvs_handle_t, const char*, const void*, size_t);
esp_err_t nvs_get_str(nvs_handle_t, const char*, char*, size_t*);
esp_err_t nvs_set_str(nvs_handle_t, const char*, const char*);
esp_err_t nvs_commit(nvs_handle_t);
esp_err_t nvs_erase_all(nvs_handle_t);
esp_err_t nvs_entry_find(const char*, const char*, int, nvs_iterator_t*);
esp_err_t nvs_entry_next(nvs_iterator_t*);
void nvs_entry_info(nvs_iterator_t, nvs_entry_info_t*);
void nvs_release_iterator(nvs_iterator_t);
