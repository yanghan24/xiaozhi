#include "device_control.h"
#include "control_transport.h"
#include "device_identity.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_app_desc.h"
#include "freertos/task.h"
#include <cassert>
#include <cstring>

namespace {
struct Finished {};
int64_t now = 1000;
void (*worker)(void*) = nullptr; void* argument = nullptr;
esp_timer_create_args_t timer{};
bool bound = true, unbinding = false, first_action = true;
int heartbeats = 0;
std::string request_body;
uint64_t original_generation = 0;
std::string State() {
    auto voice = bound ? "{\"url\":\"wss://voice.example/xiaozhi/v1/\",\"token\":\"" + std::string(43,'x') +
        "\",\"protocol_version\":1,\"permission_version\":4,\"expires_at_ms\":1300000}" : "null";
    return "{\"protocol_version\":2,\"device_id\":\"speaker-a\",\"binding_version\":" + std::string(bound ? "1" : "2") +
        ",\"permission_version\":" + (bound ? "4" : "5") + ",\"server_time_ms\":1000000,\"binding_state\":\"" +
        (bound ? "bound" : "unbound") + "\",\"use_status\":\"" + (bound ? "ready" : "unbound") +
        "\",\"voice\":" + voice + ",\"lease_expires_in_ms\":" + (bound ? "60000" : "0") +
        ",\"login_id\":null,\"customer_name\":\"customer\"}";
}
}
int64_t esp_timer_get_time() { return now * 1000; }
esp_err_t esp_timer_create(const esp_timer_create_args_t* args, esp_timer_handle_t* handle) { timer = *args; *handle = &timer; return ESP_OK; }
esp_err_t esp_timer_start_periodic(esp_timer_handle_t,uint64_t) { return ESP_OK; }
esp_err_t esp_timer_stop(esp_timer_handle_t) { return ESP_OK; }
esp_err_t esp_timer_delete(esp_timer_handle_t) { return ESP_OK; }
void esp_fill_random(void* value,size_t size) { std::memset(value,0x42,size); }
uint32_t esp_random() { return 0; }
const esp_app_desc_t* esp_app_get_description() { static esp_app_desc_t app{"fixture-version"}; return &app; }
int xTaskCreate(void (*entry)(void*),const char*,uint32_t,void* arg,unsigned,TaskHandle_t* handle) {
    worker = entry; argument = arg; *handle = arg; return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t) {}
uint32_t ulTaskNotifyTake(int,TickType_t ticks) {
    now += ticks; assert(now < 70000); timer.callback(timer.arg);
    auto& control = DeviceControl::GetInstance();
    if (!unbinding && control.IsVoiceAllowed()) {
        assert(heartbeats == 1);
        original_generation = control.GetGeneration();
        assert(control.SignOut(4)); unbinding = true;
        assert(!control.IsVoiceAllowed() && !control.IsCurrentGeneration(original_generation));
    }
    return 0;
}
void vTaskDelete(TaskHandle_t) {}
namespace device_control {
bool LoadDeviceIdentity(IdentityConfig& out) {
    out.device_id = "speaker-a"; out.credential = std::string(43,'k');
    out.ota_url = "https://speaker.example/xiaozhi/ota/";
    out.ota_origin = out.control_origin = "https://speaker.example"; return true;
}
HttpResult ControlRequest(const std::string& url,const std::string& method,const std::string& id,const std::string& secret,
        const std::string& body,const std::string&,const std::function<bool()>&) {
    assert(id == "speaker-a" && secret == std::string(43,'k'));
    assert(url.rfind("https://speaker.example/xiaozhi/speaker/v1/",0) == 0);
    HttpResult result; result.failure = HttpFailure::None; result.status = 200;
    if (url.find("/heartbeat") != std::string::npos) {
        assert(method == "POST" && body.find("fixture-version") != std::string::npos);
        ++heartbeats; result.body = "{}";
        if (!bound) { assert(!DeviceControl::GetInstance().IsVoiceAllowed()); throw Finished{}; }
    } else if (url.find("/state") != std::string::npos) result.body = State();
    else if (url.find("/actions/unbind") != std::string::npos) {
        assert(method == "POST");
        assert(!DeviceControl::GetInstance().IsVoiceAllowed());
        if (first_action) {
            request_body = body; first_action = false;
            // An uncertain outcome retries the same operation identity and expected permission.
            result.failure = HttpFailure::Timeout; result.status = 0;
        } else { assert(body == request_body); bound = false; result.body = "{\"stored\":true,\"permission_version\":5}"; }
    } else if (url.find("/login") != std::string::npos) {
        result.body = R"({"id":"12345678-1234-1234-1234-123456789012","status":"denied","verification_uri":null,"user_code":null,"expires_at_ms":1100000,"server_time_ms":1000000,"error_code":"ACCESS_DENIED"})";
    } else assert(false);
    return result;
}
}
int main() {
    auto& control = DeviceControl::GetInstance();
    control.Start("https://speaker.example/xiaozhi/ota/","https://untrusted.example/ignored-v1/");
    control.NotifyNetwork(true);
    try { worker(argument); assert(false); } catch (const Finished&) {}
    assert(heartbeats == 2 && !bound && unbinding && !first_action);
}
