#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

namespace device_control {
struct MaintenanceCommand {
    std::string operation_id;
    std::string boot_id;
    std::string url;
    std::string artifact_sha256;
    std::string target_elf_sha256;
    size_t image_size = 0;
    int64_t deadline_ms = 0;
};

// Public maintenance protocol boundary. Durable storage and the device's
// exclusive idle/upgrade gate are supplied by the platform, never by the server.
class Maintenance {
public:
    using Save = std::function<bool(const std::string&)>;
    Maintenance(std::string boot_id, std::string origin, std::string persisted,
                std::string running_elf, bool running_valid, Save save,
                std::function<bool()> reserve, std::function<void()> release,
                std::function<void(const MaintenanceCommand&)> execute);
    std::string Request(bool idle, const std::string& system_info, int64_t now_ms);
    void Apply(const std::string& response, int64_t now_ms, int64_t elapsed_ms);
    void Failed();
    void Disconnected();
    void SetRunningValid(bool valid);

private:
    std::mutex mutex_;
    std::string boot_id_, origin_, status_, origin_boot_id_;
    MaintenanceCommand command_;
    Save save_;
    std::function<bool()> reserve_;
    std::function<void()> release_;
    std::function<void(const MaintenanceCommand&)> execute_;
    bool healthy_ = true, reported_ = false, reserved_ = false, executing_ = false;
    bool running_valid_ = false;
    bool last_idle_ = false;
    bool Persist();
    void Fail();
};
}  // namespace device_control
