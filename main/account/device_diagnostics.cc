#include "device_diagnostics.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <memory>
#include <mutex>
#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_attr.h>
#include <esp_random.h>
#include <esp_system.h>
#include <esp_timer.h>

namespace account::diagnostics {
namespace {
constexpr size_t kBatchSize = 16;
constexpr uint32_t kMaxLeaseMs = 30 * 60 * 1000;
constexpr uint32_t kStateSignature = 0x58445a00;
RTC_NOINIT_ATTR uint32_t retained_state;

struct Entry {
    Event event;
    uint64_t uptime_ms;
    DeviceState state;
    int code;
    uint32_t free_heap;
    uint32_t min_free_heap;
};

std::mutex queue_mutex;
std::array<Entry, kBatchSize> queue;
size_t first = 0;
size_t count = 0;
std::atomic<uint32_t> deadline_ms{0};
std::atomic<DeviceState> current_state{kDeviceStateUnknown};
uint64_t lease_until_ms = 0;
esp_reset_reason_t reset_reason = ESP_RST_UNKNOWN;
DeviceState previous_state = kDeviceStateUnknown;
bool boot_pending = false;
std::string pending_report;

uint64_t UptimeMs() { return esp_timer_get_time() / 1000; }

bool Collecting() {
    const uint32_t deadline = deadline_ms.load();
    return deadline != 0 && static_cast<int32_t>(deadline - static_cast<uint32_t>(UptimeMs())) > 0;
}

Entry Snapshot(Event event, int code, DeviceState state) {
    return {event, UptimeMs(), state, code, esp_get_free_heap_size(), esp_get_minimum_free_heap_size()};
}

const char* EventName(Event event) {
    switch (event) {
        case Event::Boot: return "boot";
        case Event::State: return "state";
        case Event::VoiceNetworkError: return "voice_network_error";
        case Event::OtaCheckFailed: return "ota_check_failed";
        case Event::OtaUpgradeFailed: return "ota_upgrade_failed";
        case Event::HeartbeatFailed: return "heartbeat_failed";
    }
    return "state";
}

void Append(cJSON* entries, const Entry& entry) {
    auto* item = cJSON_CreateObject();
    if (!item) return;
    cJSON_AddStringToObject(item, "event", EventName(entry.event));
    cJSON_AddNumberToObject(item, "uptime_ms", entry.uptime_ms);
    cJSON_AddNumberToObject(item, "state", entry.state);
    cJSON_AddNumberToObject(item, "code", entry.code);
    cJSON_AddNumberToObject(item, "free_heap_bytes", entry.free_heap);
    cJSON_AddNumberToObject(item, "min_free_heap_bytes", entry.min_free_heap);
    cJSON_AddItemToArray(entries, item);
}

void StopCollection() {
    deadline_ms = 0;
    lease_until_ms = 0;
    boot_pending = false;
    pending_report.clear();
    std::lock_guard<std::mutex> lock(queue_mutex);
    first = count = 0;
    retained_state = 0;
}
}

void Initialize() {
    reset_reason = esp_reset_reason();
    const uint32_t state = retained_state & 0xff;
    if (reset_reason != ESP_RST_POWERON && (retained_state & ~0xffU) == kStateSignature
            && state <= kDeviceStateFatalError) {
        previous_state = static_cast<DeviceState>(state);
    }
    retained_state = 0;
}

void ObserveState(DeviceState state) {
    current_state = state;
    Record(Event::State);
}

void Record(Event event, int code) {
    if (!Collecting()) return;
    std::unique_lock<std::mutex> lock(queue_mutex, std::try_to_lock);
    if (!lock || !Collecting()) return;
    const auto state = current_state.load();
    if (event == Event::State) retained_state = kStateSignature | static_cast<uint32_t>(state);
    queue[(first + count) % queue.size()] = Snapshot(event, code, state);
    if (count < queue.size()) ++count;
    else first = (first + 1) % queue.size();
}

void ApplyHeartbeat(const std::string& response) {
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> doc(cJSON_Parse(response.c_str()), cJSON_Delete);
    auto* until = cJSON_GetObjectItemCaseSensitive(doc.get(), "diagnostics_until_ms");
    auto* now = cJSON_GetObjectItemCaseSensitive(doc.get(), "server_time_ms");
    if (!cJSON_IsNumber(until) || !cJSON_IsNumber(now) || now->valuedouble <= 0
            || until->valuedouble <= now->valuedouble || until->valuedouble >= 1e15
            || until->valuedouble - now->valuedouble > kMaxLeaseMs) {
        if (lease_until_ms) StopCollection();
        return;
    }
    const auto lease = static_cast<uint64_t>(until->valuedouble);
    if (lease != lease_until_ms) {
        StopCollection();
        lease_until_ms = lease;
        boot_pending = true;
        std::lock_guard<std::mutex> lock(queue_mutex);
        retained_state = kStateSignature | static_cast<uint32_t>(current_state.load());
    }
    deadline_ms = static_cast<uint32_t>(UptimeMs() + static_cast<uint64_t>(until->valuedouble - now->valuedouble));
}

const std::string& PendingReport() {
    if (!Collecting()) {
        if (lease_until_ms) StopCollection();
        return pending_report;
    }
    if (!pending_report.empty()) return pending_report;
    std::array<Entry, kBatchSize> entries;
    size_t size = 0;
    if (boot_pending) entries[size++] = Snapshot(Event::Boot, reset_reason, previous_state);
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        while (count && size < entries.size()) {
            entries[size++] = queue[first];
            first = (first + 1) % queue.size();
            --count;
        }
    }
    if (!size) return pending_report;

    unsigned char random[16];
    esp_fill_random(random, sizeof(random));
    char report_id[sizeof(random) * 2 + 1];
    for (size_t i = 0; i < sizeof(random); ++i) snprintf(report_id + i * 2, 3, "%02x", random[i]);
    const auto* app = esp_app_get_description();
    char hash[sizeof(app->app_elf_sha256) * 2 + 1];
    for (size_t i = 0; i < sizeof(app->app_elf_sha256); ++i) snprintf(hash + i * 2, 3, "%02x", app->app_elf_sha256[i]);
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> body(cJSON_CreateObject(), cJSON_Delete);
    if (!body) return pending_report;
    cJSON_AddStringToObject(body.get(), "report_id", report_id);
    cJSON_AddNumberToObject(body.get(), "diagnostics_until_ms", lease_until_ms);
    cJSON_AddStringToObject(body.get(), "firmware_version", app->version);
    cJSON_AddStringToObject(body.get(), "app_sha256", hash);
    auto* list = cJSON_AddArrayToObject(body.get(), "entries");
    if (!list) return pending_report;
    for (size_t i = 0; i < size; ++i) Append(list, entries[i]);
    auto* encoded = cJSON_PrintUnformatted(body.get());
    if (encoded) {
        pending_report = encoded;
        cJSON_free(encoded);
        boot_pending = false;
    }
    return pending_report;
}

void CompleteReport(int status) {
    if (status == 401 || status == 403 || status == 409) StopCollection();
    else if (status == 200 || status == 400 || status == 413) pending_report.clear();
}
}
