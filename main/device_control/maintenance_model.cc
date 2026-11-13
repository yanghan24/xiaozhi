#include "maintenance_model.h"
#include <cJSON.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include "control_model.h"

namespace device_control {
namespace {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
Json Parse(const std::string& text) {
    return Json(text.size() <= 16384 ? cJSON_Parse(text.c_str()) : nullptr, cJSON_Delete);
}
std::string String(const cJSON* object, const char* key) {
    auto item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) ? item->valuestring : "";
}
int64_t Number(const cJSON* object, const char* key) {
    auto item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsNumber(item) && std::isfinite(item->valuedouble) && item->valuedouble >= 0 &&
                   item->valuedouble <= 9007199254740991.0 &&
                   std::floor(item->valuedouble) == item->valuedouble
               ? static_cast<int64_t>(item->valuedouble)
               : -1;
}
bool Hex(const std::string& value, size_t length) {
    return value.size() == length && std::all_of(value.begin(), value.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
std::string Encode(cJSON* object) {
    char* bytes = cJSON_PrintUnformatted(object);
    std::string result = bytes ? bytes : "";
    cJSON_free(bytes);
    return result;
}
bool Status(const std::string& status) {
    return status == "accepted" || status == "failed" || status == "interrupted" ||
           status == "installed";
}
}  // namespace
Maintenance::Maintenance(std::string boot, std::string origin, std::string persisted,
                         std::string running_elf, bool running_valid, Save save,
                         std::function<bool()> reserve, std::function<void()> release,
                         std::function<void(const MaintenanceCommand&)> execute)
    : boot_id_(std::move(boot)),
      origin_(std::move(origin)),
      save_(std::move(save)),
      reserve_(std::move(reserve)),
      release_(std::move(release)),
      execute_(std::move(execute)) {
    healthy_ = Hex(boot_id_, 32);
    running_valid_ = running_valid;
    if (persisted.empty())
        return;
    auto root = Parse(persisted);
    status_ = String(root.get(), "status");
    origin_boot_id_ = String(root.get(), "origin_boot_id");
    command_.operation_id = String(root.get(), "operation_id");
    command_.target_elf_sha256 = String(root.get(), "target_elf_sha256");
    command_.artifact_sha256 = String(root.get(), "artifact_sha256");
    auto size = Number(root.get(), "image_size");
    if (!root || Number(root.get(), "version") != 1 || !Status(status_) ||
        !Hex(origin_boot_id_, 32) || !Hex(command_.operation_id, 32) ||
        !Hex(command_.target_elf_sha256, 64) || !Hex(command_.artifact_sha256, 64) || size < 1 ||
        size > 16 * 1024 * 1024) {
        healthy_ = false;
        status_.clear();
        return;
    }
    command_.image_size = size;
    reported_ = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root.get(), "reported"));
    if (status_ == "accepted") {
        // A durable acceptance is never replayed after restart, even if the
        // server did not receive the final acknowledgement before power loss.
        status_ = running_valid && running_elf == command_.target_elf_sha256 ? "installed"
                                                                             : "interrupted";
        reported_ = false;
        healthy_ = Persist() && healthy_;
    }
}
bool Maintenance::Persist() {
    Json root(cJSON_CreateObject(), cJSON_Delete);
    cJSON_AddNumberToObject(root.get(), "version", 1);
    cJSON_AddStringToObject(root.get(), "operation_id", command_.operation_id.c_str());
    cJSON_AddStringToObject(root.get(), "origin_boot_id", origin_boot_id_.c_str());
    cJSON_AddStringToObject(root.get(), "status", status_.c_str());
    cJSON_AddStringToObject(root.get(), "target_elf_sha256", command_.target_elf_sha256.c_str());
    cJSON_AddStringToObject(root.get(), "artifact_sha256", command_.artifact_sha256.c_str());
    cJSON_AddNumberToObject(root.get(), "image_size", command_.image_size);
    cJSON_AddBoolToObject(root.get(), "reported", reported_);
    return save_(Encode(root.get()));
}
void Maintenance::Fail() {
    if (status_ != "accepted")
        return;
    status_ = "failed";
    reported_ = false;
    healthy_ = Persist() && healthy_;
    if (reserved_)
        release_();
    reserved_ = false;
    executing_ = false;
}
std::string Maintenance::Request(bool idle, const std::string& system_info, int64_t now_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (reserved_ && !executing_ && now_ms >= command_.deadline_ms)
        Fail();
    last_idle_ = idle;
    auto info = Parse(system_info);
    if (!cJSON_IsObject(info.get()))
        return "";
    Json root(cJSON_CreateObject(), cJSON_Delete);
    cJSON_AddStringToObject(root.get(), "boot_id", boot_id_.c_str());
    cJSON_AddBoolToObject(root.get(), "ota_capable", healthy_ && running_valid_);
    cJSON_AddBoolToObject(root.get(), "idle", idle && !executing_);
    cJSON_AddItemToObject(root.get(), "system_info", info.release());
    if (!status_.empty() && !reported_) {
        auto op = cJSON_AddObjectToObject(root.get(), "operation");
        cJSON_AddStringToObject(op, "operation_id", command_.operation_id.c_str());
        cJSON_AddStringToObject(op, "origin_boot_id", origin_boot_id_.c_str());
        cJSON_AddStringToObject(op, "status", status_.c_str());
    } else {
        cJSON_AddNullToObject(root.get(), "operation");
    }
    return Encode(root.get());
}
void Maintenance::Apply(const std::string& response, int64_t now_ms, int64_t elapsed_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    auto root = Parse(response);
    auto data = cJSON_GetObjectItemCaseSensitive(root.get(), "data");
    if (!root || Number(root.get(), "version") != 1 || String(root.get(), "code") != "OK" ||
        !cJSON_IsObject(data) || Number(data, "server_time_ms") < 0 || elapsed_ms < 0)
        return;
    auto ack = cJSON_GetObjectItemCaseSensitive(data, "operation_ack");
    if (!status_.empty() && !reported_ && String(ack, "operation_id") == command_.operation_id &&
        String(ack, "status") == status_) {
        if (status_ != "accepted") {
            reported_ = true;
            healthy_ = Persist() && healthy_;
        } else if (reserved_ && !executing_) {
            const auto remaining = Number(ack, "expires_in_ms") - elapsed_ms;
            if (remaining <= 0 || now_ms >= command_.deadline_ms ||
                Number(ack, "expires_at_ms") <= Number(data, "server_time_ms")) {
                Fail();
                return;
            }
            command_.deadline_ms = std::min(command_.deadline_ms, now_ms + remaining);
            executing_ = true;
            auto command = command_;
            lock.unlock();
            execute_(command);
            return;
        }
    }
    auto offer = cJSON_GetObjectItemCaseSensitive(data, "command");
    if (!healthy_ || !running_valid_ || reserved_ || !last_idle_ ||
        (!status_.empty() && !reported_) || !cJSON_IsObject(offer))
        return;
    MaintenanceCommand command;
    command.operation_id = String(offer, "operation_id");
    command.boot_id = String(offer, "boot_id");
    command.url = String(offer, "url");
    command.artifact_sha256 = String(offer, "artifact_sha256");
    command.target_elf_sha256 = String(offer, "target_elf_sha256");
    auto size = Number(offer, "image_size");
    auto expiry = Number(offer, "expires_in_ms");
    Url url;
    if (!Hex(command.operation_id, 32) || command.operation_id == command_.operation_id ||
        command.boot_id != boot_id_ || !Hex(command.artifact_sha256, 64) ||
        !Hex(command.target_elf_sha256, 64) || size < 1 || size > 16 * 1024 * 1024 ||
        expiry <= elapsed_ms || expiry > 600000 ||
        Number(offer, "expires_at_ms") <= Number(data, "server_time_ms") ||
        command.url.size() > 2048 || !ParseUrl(command.url, url) || url.scheme != "https" ||
        url.Origin() != origin_ || !reserve_())
        return;
    command.image_size = size;
    command.deadline_ms = now_ms + expiry - elapsed_ms;
    command_ = std::move(command);
    origin_boot_id_ = boot_id_;
    status_ = "accepted";
    reported_ = false;
    reserved_ = true;
    if (!Persist()) {
        healthy_ = false;
        Fail();
    }
}
void Maintenance::Failed() {
    std::lock_guard<std::mutex> lock(mutex_);
    Fail();
}
void Maintenance::Disconnected() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!executing_)
        Fail();
}
void Maintenance::SetRunningValid(bool valid) {
    std::lock_guard<std::mutex> lock(mutex_);
    running_valid_ = valid;
}
}  // namespace device_control
