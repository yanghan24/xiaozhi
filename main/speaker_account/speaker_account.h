#ifndef SPEAKER_ACCOUNT_H
#define SPEAKER_ACCOUNT_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

class SpeakerAccount {
public:
    struct Identity {
        std::string id;
        std::string credential;
        std::string origin;

        std::string OtaUrl() const;
    };

    struct VoiceAccess {
        std::string url;
        std::string token;
        int protocol_version = 1;
    };

    using MemoOwner = std::array<char, 32>;
    struct MemoAccess {
        int64_t binding_version = 0;
        MemoOwner owner{};

        bool operator==(const MemoAccess&) const = default;
    };

    enum class Page {
        Connecting,
        Unprovisioned,
        Scan,
        Scanned,
        Retry,
        Disabled,
        Ready,
        Offline,
    };

    struct View {
        Page page = Page::Connecting;
        std::string qr_url;
        std::string user_code;
        std::string customer;
        bool can_unbind = false;
        bool unbind_pending = false;
        bool standby_weather_allowed = false;
        int64_t balance_permission = 0;
        bool balance_loading = false;
        std::string balance;
        bool recharge_available = false;

        bool operator==(const View&) const = default;
    };

    static SpeakerAccount& Get();
    static constexpr const char* RegisteredBoard() {
        return "waveshare-esp32-p4-wifi6-touch-lcd-4.3";
    }

    void LoadIdentity();
    Identity identity() const;
    const std::string& hardware_id() const { return hardware_id_; }
    void NetworkChanged(bool connected);
    void Start();
    void SetUpgrading(bool upgrading);
    std::optional<VoiceAccess> Voice() const;
    View GetView() const;
    MemoAccess GetMemoAccess() const;
    void RefreshCode();
    void RefreshBalance();
    void Unbind();

private:
    friend class SpeakerStandbyWeather;
    friend class SpeakerRecharge;
    struct Response {
        int status = 0;
        std::string body;
    };

    void Run();
    void FetchBalance();
    Response Request(const char* method, const std::string& path, std::string body = {});
    Response RequestWithIdentity(const Identity& identity, const char* method, const std::string& path,
                                 std::string body = {}, int channel = 5, int timeout_ms = 5000);
    enum class LoginResult { InProgress, Completed, InvalidCredential };
    LoginResult PollLogin(const Identity& identity, std::string& login_id, bool& retry_login, bool& replace_login,
                          const std::string& customer, bool bound, int64_t permission, uint32_t epoch);
    void Publish(View view, std::optional<VoiceAccess> access, int64_t lease_until,
                 int64_t permission_version, bool revoke_if_changed, uint32_t network_epoch,
                 int64_t binding_version = 0, MemoOwner memo_owner = {});
    void Notify();

    Identity identity_;
    std::string hardware_id_;
    mutable std::mutex mutex_;
    TaskHandle_t worker_ = nullptr;
    View view_;
    std::optional<VoiceAccess> voice_;
    int64_t voice_until_ = 0;
    int64_t permission_version_ = 0;
    MemoAccess memo_access_{};
    uint32_t network_epoch_ = 0;
    bool enrollment_allowed_ = false;
    bool connected_ = false;
    bool upgrading_ = false;
    bool refresh_ = false;
    bool unbind_ = false;
    bool balance_task_running_ = false;
};

void RenderSpeakerAccount(const SpeakerAccount::View& view);

#endif
