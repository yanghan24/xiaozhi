#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include "control_transport.h"
#include "device_control.h"
#include "device_identity.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/task.h"

namespace {
// Run the real worker cooperatively: only RTOS scheduling, time, identity and
// transport are replaced. Stop at its next state request, before a good server
// response could hide a failure to revoke the previous lease.
struct Finished {};
void (*worker)(void*) = nullptr;
void* worker_arg = nullptr;
esp_timer_create_args_t lease_timer{};
int64_t now_ms = 1000;
std::string operation;
std::string outcome;
device_control::HttpResult response;
bool sent_operation = false;
bool opened = false;
bool maintenance_replied = false;
unsigned revocations = 0;
unsigned previous_revocations = 0;
uint64_t previous_generation = 0;

void Check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}

const char* AllowedState =
    R"({"version":1,"code":"OK","data":{"device_id":"speaker-a","revision":28,"permission_version":4,"server_time_ms":1000000,"binding_state":"bound","use_permission":"allowed","reason":"ready","binding_request":null,"lease":{"permission_version":4,"expires_at_ms":1060000,"expires_in_ms":60000},"voice":{"url":"wss://speaker.example:8443/xiaozhi/v1/","token":"voice-token","expires_at_ms":1300000,"protocol_version":1,"permission_version":4}}})";
const char* NullableAccount =
    R"({"version":1,"code":"OK","data":{"device_id":"speaker-a","permission_version":4,"username":null,"balance":null}})";
const char* UnboundState =
    R"({"version":1,"code":"OK","data":{"device_id":"speaker-a","revision":29,"permission_version":5,"server_time_ms":1000000,"binding_state":"unbound","use_permission":"blocked","reason":"unbound","binding_request":null,"lease":null,"voice":null}})";

bool Released() {
    return outcome == "success" || outcome == "signed-out" || outcome == "reauth" ||
           outcome == "auth-pending";
}

std::string InitialState() {
    if (outcome != "signed-out" && outcome != "reauth" && outcome != "auth-pending")
        return AllowedState;
    const std::string reason = outcome == "signed-out" ? "user_signed_out"
                               : outcome == "reauth"  ? "market_auth_required"
                                                       : "market_auth_pending";
    return R"({"version":1,"code":"OK","data":{"device_id":"speaker-a","revision":28,"permission_version":4,"server_time_ms":1000000,"binding_state":"bound","use_permission":"blocked","reason":")" +
           reason + R"(","binding_request":null,"lease":null,"voice":null}})";
}

bool Fatal() {
    return outcome == "unauthorized" || outcome == "forbidden" || outcome == "tls" ||
           outcome == "invalid";
}

void CheckBeforeState() {
    auto& control = DeviceControl::GetInstance();
    Check(now_ms < 10000, "test must finish while the original 60-second lease is valid");
    if (Fatal()) {
        Check(revocations > previous_revocations,
              "fatal menu response must invoke the audio revocation callback before state returns");
        Check(!control.IsVoiceAllowed() && !control.GetView().voice_allowed,
              "fatal menu response must close voice permission before state returns");
        Check(control.GetGeneration() > previous_generation &&
                  !control.IsCurrentGeneration(previous_generation),
              "fatal menu response must invalidate the old audio generation");
        DeviceVoiceConfig voice;
        Check(!control.GetVoiceConfig(voice), "fatal menu response must hide the old voice token");
    } else if (operation == "maintenance") {
        Check(maintenance_replied && control.IsVoiceAllowed(),
              "missing or unavailable maintenance must preserve ordinary voice and state polling");
    } else if (operation == "account") {
        Check(revocations == previous_revocations && control.IsVoiceAllowed() &&
                  control.GetGeneration() == previous_generation,
              "benign account failure must preserve the original voice lease");
        const auto account = control.GetAccountView();
        Check(account.username.empty() && account.balance.empty(),
              "nullable or unavailable account data must not invent display values");
        if (outcome == "nullable")
            Check(account.status == DeviceAccountStatus::Ready,
                  "HTTP 200 with null display fields must remain a valid account result");
    } else {
        Check(!control.IsVoiceAllowed(),
              "sign-out must remain locally stopped until state confirms");
    }
    throw Finished{};
}
}  // namespace

int64_t esp_timer_get_time() { return now_ms * 1000; }
esp_err_t esp_timer_create(const esp_timer_create_args_t* args, esp_timer_handle_t* timer) {
    lease_timer = *args;
    *timer = &lease_timer;
    return ESP_OK;
}
esp_err_t esp_timer_start_periodic(esp_timer_handle_t, uint64_t) { return ESP_OK; }
esp_err_t esp_timer_stop(esp_timer_handle_t) { return ESP_OK; }
esp_err_t esp_timer_delete(esp_timer_handle_t) { return ESP_OK; }
void esp_fill_random(void* buffer, size_t size) { std::memset(buffer, 0x42, size); }
uint32_t esp_random() { return 0; }
int xTaskCreate(void (*entry)(void*), const char*, uint32_t, void* arg, unsigned,
                TaskHandle_t* handle) {
    worker = entry;
    worker_arg = arg;
    *handle = arg;
    return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t) {}
uint32_t ulTaskNotifyTake(int, TickType_t ticks) {
    now_ms += ticks;
    Check(now_ms < 10000, "worker did not reach the next state request");
    lease_timer.callback(lease_timer.arg);
    return 0;
}
void vTaskDelete(TaskHandle_t) {}

namespace device_control {
bool LoadDeviceIdentity(IdentityConfig& identity) {
    identity.device_id = "speaker-a";
    identity.credential = "test-only-device-credential";
    identity.ota_origin = "https://speaker.example:8443";
    return true;
}
HttpResult ControlRequest(const std::string& url, const std::string& method, const std::string& id,
                          const std::string& credential, const std::string& body,
                          const std::string&, const std::function<bool()>& cancelled) {
    Check(!cancelled(), "selected request must use the current control epoch");
    Check(id == "speaker-a" && credential == "test-only-device-credential",
          "worker must use the provisioned device identity");
    if (url.find("/state") != std::string::npos) {
        Check(method == "GET", "state synchronization must use GET");
        if (sent_operation && Released())
            return {200, UnboundState, {}, {}, HttpFailure::None, 1};
        if (sent_operation)
            CheckBeforeState();
        return {200, InitialState(), {}, {}, HttpFailure::None, 1};
    }
    Check(url == "https://speaker.example:8443/xiaozhi/device-control/v1/" + operation,
          "worker chose an unexpected menu endpoint");
    Check(!sent_operation, "worker must synchronize state before retrying the menu operation");
    Check(method == (operation == "account" ? "GET" : "POST"), "wrong menu method");
    if (operation == "unbind")
        Check(body == R"({"expected_permission_version":4})",
              "sign-out must retain the explicitly confirmed permission version");
    sent_operation = true;
    auto& control = DeviceControl::GetInstance();
    previous_revocations = revocations;
    previous_generation = control.GetGeneration();
    if (operation == "maintenance" && outcome == "cancelled-success")
        control.Refresh();
    return response;
}
}  // namespace device_control

int main(int argc, char** argv) {
    try {
        Check(argc == 3, "usage: control_worker_tests account|unbind|maintenance outcome");
        operation = argv[1];
        outcome = argv[2];
        Check(operation == "account" || operation == "unbind" || operation == "maintenance",
              "unknown operation");
        response.failure = device_control::HttpFailure::None;
        if (outcome == "unauthorized")
            response.status = 401;
        else if (outcome == "forbidden")
            response.status = 403;
        else if (outcome == "tls")
            response.failure = device_control::HttpFailure::Tls;
        else if (outcome == "invalid")
            response.failure = device_control::HttpFailure::Invalid;
        else if (outcome == "missing")
            response.status = 404;
        else if (outcome == "server")
            response.status = 503;
        else if (outcome == "network")
            response.failure = device_control::HttpFailure::Network;
        else if (outcome == "timeout")
            response.failure = device_control::HttpFailure::Timeout;
        else if (outcome == "cancelled-success")
            response.status = 200;
        else if (outcome == "nullable") {
            response.status = 200;
            response.body = NullableAccount;
        } else if (Released()) {
            response.status = 200;
            response.body = R"({"version":1,"code":"OK","data":{"permission_version":5,"binding_state":"unbound"}})";
        } else
            Check(false, "unknown outcome");
        auto& control = DeviceControl::GetInstance();
        if (operation == "maintenance") {
            control.SetMaintenanceCallbacks(
                [] { return "{}"; },
                [&](const device_control::HttpResult& result) {
                    maintenance_replied = true;
                    if (outcome == "cancelled-success")
                        Check(result.failure == device_control::HttpFailure::Cancelled,
                              "a response from an interrupted control epoch must not reach the "
                              "command parser as success");
                });
        }
        control.SetCallbacks(
            [&](const DeviceControlView& view) {
                if (sent_operation && Released() && view.reason == "unbound") {
                    Check(!view.sign_out_pending && !control.IsVoiceAllowed(),
                          "confirmed unbinding stays silent and clears the pending operation");
                    const auto account = control.GetAccountView();
                    Check(account.can_scan && !account.can_sign_out && account.username.empty() &&
                              account.balance.empty(),
                          "released device offers a new login without any previous account data");
                    throw Finished{};
                }
                if (view.permission_version != 4 || opened)
                    return;
                opened = true;
                if (operation == "account")
                    control.ShowAccountDetails(true);
                else if (operation == "unbind")
                    Check(control.SignOut(view.permission_version), "local sign-out must start");
            },
            [&](uint64_t) { ++revocations; });
        control.Start("https://speaker.example:8443/xiaozhi/ota/",
                      "https://speaker.example:8443/xiaozhi/device-control/v1/");
        control.NotifyNetwork(true);
        Check(worker != nullptr, "control task was not started");
        worker(worker_arg);
        Check(false, "worker ended without checking the menu response");
    } catch (const Finished&) {
        std::cout << operation << ' ' << outcome << " worker regression passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << operation << ' ' << outcome << ": " << error.what() << '\n';
    }
    return 1;
}
