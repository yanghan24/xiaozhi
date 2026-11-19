#include "device_account.h"
#include "device_diagnostics.h"
#include "application.h"
#include "board.h"
#include "settings.h"
#include "display.h"
#include "lvgl_theme.h"
#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <nvs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <set>
#include <memory>
#include <cstdio>
#include <cstdlib>

namespace account {
namespace {
uint64_t Now() { return esp_timer_get_time() / 1000; }
std::string Print(cJSON* json) {
    char* value = cJSON_PrintUnformatted(json);
    std::string result = value ? value : "";
    cJSON_free(value);cJSON_Delete(json);return result;
}
std::string HardwareId() {
    uint8_t mac[6] = {};
    if (esp_efuse_mac_get_default(mac) != ESP_OK) return {};
    char value[13];snprintf(value,sizeof(value),"%02x%02x%02x%02x%02x%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);return value;
}
std::string NewOperation() {
    uint32_t random[4];esp_fill_random(random,sizeof(random));char value[33];
    snprintf(value,sizeof(value),"%08lx%08lx%08lx%08lx",(unsigned long)random[0],(unsigned long)random[1],(unsigned long)random[2],(unsigned long)random[3]);return value;
}
// UI objects are touched only from Application's scheduled task, under its display lock.
lv_obj_t* panel = nullptr;
lv_obj_t* account_button = nullptr;
bool account_details = false;
bool confirm_unbind = false;
void Button(lv_obj_t* parent, const char* text, lv_event_cb_t callback) {
    auto button = lv_button_create(parent);
    lv_obj_set_size(button,LV_PCT(85),54);
    auto label = lv_label_create(button);lv_label_set_text(label,text);lv_obj_center(label);
    lv_obj_add_event_cb(button,callback,LV_EVENT_CLICKED,nullptr);
}
const char* StatusText(const std::string& status) {
    if (status=="admin_disabled") return "设备已被管理员停用";
    if (status=="default_agent_required" || status=="voice_not_configured") return "请联系管理员配置默认智能体和语音服务";
    if (status=="signed_out") return "请原归属账号扫码恢复本台音箱";
    if (status=="authorization_required") return "请原归属账号重新扫码授权";
    if (status=="unbound") return "请使用 AI 集市账号扫码登录";
    return "正在获取音箱登录状态";
}
}

bool DeviceAccount::LoadIdentity() {
    nvs_handle_t handle;
    if (nvs_open("device_ctrl",NVS_READONLY,&handle)!=ESP_OK) return false;
    size_t size=0;auto error=nvs_get_blob(handle,"identity_v1",nullptr,&size);
    std::string data;
    if (error==ESP_OK && size>0 && size<=2048) {
        data.resize(size);error=nvs_get_blob(handle,"identity_v1",data.data(),&size);
    } else error=ESP_ERR_INVALID_SIZE;
    nvs_close(handle);
    if (error!=ESP_OK || !ParseIdentity(data,HardwareId(),identity_)) return false;
    Settings settings("device_ctrl");
    const auto anchor=settings.GetString("device_id");
    if (!anchor.empty() && anchor!=identity_.device_id) { identity_={};return false; }
    return true;
}

void DeviceAccount::NetworkChanged(bool connected) {
    connected_=connected;
    if (!started_) {
        started_=true;initialized_=LoadIdentity();
        if (!initialized_) message_="设备身份尚未交付，请联系管理员。硬件标识："+HardwareId();
        else {
            Settings settings("device_ctrl");
            pending_unbind_=settings.GetString("unbind_request");
            const auto version=settings.GetString("unbind_version");
            if (!pending_unbind_.empty() && !version.empty()) pending_version_=strtoull(version.c_str(),nullptr,10);
            xTaskCreate([](void* self) { static_cast<DeviceAccount*>(self)->Worker(); },"account",8192,this,2,nullptr);
        }
    }
    if (!connected) {
        std::lock_guard<std::mutex> lock(mutex_);state_.ready=false;state_.websocket_token.clear();
        Application::GetInstance().Schedule([] { Application::GetInstance().OnAccountUnavailable(); });
    }
    Render();
}

bool DeviceAccount::Ready() {
    std::lock_guard<std::mutex> lock(mutex_);
    return connected_ && state_.ready && Now()<state_.valid_until_ms && pending_unbind_.empty() && !unbind_;
}
bool DeviceAccount::Connection(std::string& url,std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_ || !state_.ready || Now()>=state_.valid_until_ms || !pending_unbind_.empty() || unbind_) return false;
    url=state_.websocket_url;token=state_.websocket_token;return true;
}
void DeviceAccount::RetryLogin() { retry_=true; }
void DeviceAccount::RequestUnbind() {
    { std::lock_guard<std::mutex> lock(mutex_);state_.ready=false;message_="正在退出并解除本台绑定"; }
    unbind_=true;
    Application::GetInstance().Schedule([] { Application::GetInstance().OnAccountUnavailable(); });
}
void DeviceAccount::ShowAccount() { account_details=true;Render(); }

int DeviceAccount::Request(const char* method,const std::string& path,const std::string& body,std::string& response,
                           int timeout_ms) {
    response.clear();
    auto http=Board::GetInstance().GetNetwork()->CreateHttp(5);
    if (!http) return 0;
    http->SetTimeout(timeout_ms);
    http->SetHeader("Device-Id",identity_.device_id);
    http->SetHeader("Authorization","Bearer "+identity_.credential);
    http->SetHeader("Content-Type","application/json");
    if (!body.empty()) http->SetContent(std::string(body));
    if (!http->Open(method,identity_.origin+"/xiaozhi/speaker/v1"+path)) return 0;
    int code=http->GetStatusCode();
    char data[512];int bytes;
    while ((bytes=http->Read(data,sizeof(data)))>0) {
        if (response.size()+bytes>65536) { code=0;break; }
        response.append(data,bytes);
    }
    http->Close();return bytes<0?0:code;
}

void DeviceAccount::Apply(const State& next) {
    bool revoke;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        revoke=state_.ready && (!next.ready || state_.permission_version!=next.permission_version);
        state_=next;
        const bool needs_login=next.use_status=="unbound" || next.use_status=="signed_out" || next.use_status=="authorization_required";
        if (!needs_login) login_={};
        if (next.ready) message_.clear();
    }
    if (revoke) Application::GetInstance().Schedule([] { Application::GetInstance().OnAccountUnavailable(); });
}

void DeviceAccount::Worker() {
    uint64_t heartbeat=0,poll=0;
    bool attempted_login=false;
    for (;;) {
        if (!connected_) { vTaskDelay(pdMS_TO_TICKS(1000));continue; }
        const auto now=Now();std::string response;
        bool can_upload_diagnostics = false;
        if (unbind_.exchange(false)) {
            uint64_t version;
            { std::lock_guard<std::mutex> lock(mutex_);version=state_.permission_version; }
            if (version>0) {
                auto operation=NewOperation();
                Settings settings("device_ctrl",true);settings.SetString("unbind_request",operation);settings.SetString("unbind_version",std::to_string(version));
                std::lock_guard<std::mutex> lock(mutex_);pending_unbind_=operation;pending_version_=version;state_.ready=false;
            }
        }
        if (!pending_unbind_.empty()) {
            auto* body=cJSON_CreateObject();cJSON_AddNumberToObject(body,"expected_permission_version",pending_version_);cJSON_AddStringToObject(body,"request_key",pending_unbind_.c_str());
            int result=Request("POST","/actions/unbind",Print(body),response);
            if (result==200 || result==409) {
                Settings settings("device_ctrl",true);settings.EraseKey("unbind_request");settings.EraseKey("unbind_version");
                std::lock_guard<std::mutex> lock(mutex_);pending_unbind_.clear();login_={};
                message_=result==409?"设备状态已变化，请确认账号后重新操作":"已退出并解除本台绑定";
                attempted_login=false;poll=0;
            } else {
                { std::lock_guard<std::mutex> lock(mutex_);message_="正在退出并解绑，联网后继续确认"; }
                Render();vTaskDelay(pdMS_TO_TICKS(5000));continue;
            }
        }
        if (now>=heartbeat) {
            auto* body=cJSON_CreateObject();cJSON_AddStringToObject(body,"board_type",BoardType());cJSON_AddStringToObject(body,"firmware_version",esp_app_get_description()->version);
            cJSON_AddBoolToObject(body, "diagnostics_supported", true);
            const int status = Request("POST", "/heartbeat", Print(body), response);
            if (status == 200) {
                diagnostics::ApplyHeartbeat(response);
                can_upload_diagnostics = true;
            } else {
                diagnostics::Record(diagnostics::Event::HeartbeatFailed, status);
            }
            heartbeat=now+20000;
        }
        if (now>=poll || retry_) {
            State next;
            int code=Request("GET","/state","",response);
            if (code==200 && ParseState(response,identity_.device_id,Now(),next)) {
                Apply(next);
                const bool login_needed=next.use_status=="unbound" || next.use_status=="signed_out" || next.use_status=="authorization_required";
                if (login_needed && (!attempted_login || retry_.exchange(false))) {
                    attempted_login=true;
                    Login login;
                    if (Request("POST","/login","{}",response)==200 && ParseLogin(response,Now(),login)) {
                        std::lock_guard<std::mutex> lock(mutex_);login_=login;message_.clear();
                    } else { std::lock_guard<std::mutex> lock(mutex_);message_="暂时无法获取登录二维码，请重试"; }
                } else {
                    retry_=false;
                    std::string id;
                    { std::lock_guard<std::mutex> lock(mutex_);id=login_.id; }
                    if (login_needed && !id.empty()) {
                        Login login;
                        if (Request("GET","/login/"+id,"",response)==200 && ParseLogin(response,Now(),login)) {
                            std::lock_guard<std::mutex> lock(mutex_);login_=login;
                        }
                    }
                }
            } else if (code==401 || code==403) {
                Apply(State{});
                std::lock_guard<std::mutex> lock(mutex_);login_={};message_="设备身份不可用，请联系管理员";
            }
            poll=Now()+5000;Render();
        }
        bool expired=false;
        { std::lock_guard<std::mutex> lock(mutex_);if (state_.ready && Now()>=state_.valid_until_ms) { state_.ready=false;expired=true; } }
        if (expired) { Application::GetInstance().Schedule([] { Application::GetInstance().OnAccountUnavailable(); });Render(); }
        if (can_upload_diagnostics && connected_) {
            const auto& report = diagnostics::PendingReport();
            if (!report.empty()) {
                const int status = Request("POST", "/diagnostics", report, response, 1500);
                diagnostics::CompleteReport(status);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void DeviceAccount::Render() {
    State state;Login login;std::string message;bool pending;
    { std::lock_guard<std::mutex> lock(mutex_);state=state_;login=login_;message=message_;pending=!pending_unbind_.empty(); }
    const bool online=connected_;
    Application::GetInstance().Schedule([this,state,login,message,pending,online] {
        auto& app=Application::GetInstance();auto display=Board::GetInstance().GetDisplay();
        if (!display || !display->IsSetupUICalled()) return;
        DisplayLockGuard lock(display);
        const bool ready=state.ready && online && Now()<state.valid_until_ms;
        static uint64_t shown_version=0;
        if (!ready || shown_version!=state.permission_version) { confirm_unbind=false;account_details=false; }
        shown_version=state.permission_version;
        if (panel) { lv_obj_delete(panel);panel=nullptr; }
        if (account_button) { lv_obj_delete(account_button);account_button=nullptr; }
        if (app.GetDeviceState()==kDeviceStateUpgrading) return;
        if (ready && !account_details && !pending) {
            account_button=lv_button_create(lv_layer_top());lv_obj_set_size(account_button,100,44);lv_obj_align(account_button,LV_ALIGN_TOP_RIGHT,-8,42);
            auto label=lv_label_create(account_button);lv_label_set_text(label,"账号");lv_obj_center(label);
            if (auto theme=static_cast<LvglTheme*>(display->GetTheme())) lv_obj_set_style_text_font(account_button,theme->text_font()->font(),0);
            lv_obj_add_event_cb(account_button,[](lv_event_t*) { DeviceAccount::GetInstance().ShowAccount(); },LV_EVENT_CLICKED,nullptr);return;
        }
        panel=lv_obj_create(lv_layer_top());lv_obj_set_size(panel,LV_PCT(100),LV_PCT(100));lv_obj_center(panel);
        lv_obj_set_style_bg_color(panel,lv_color_white(),0);lv_obj_set_style_bg_opa(panel,LV_OPA_COVER,0);lv_obj_set_style_text_color(panel,lv_color_black(),0);
        lv_obj_set_flex_flow(panel,LV_FLEX_FLOW_COLUMN);lv_obj_set_flex_align(panel,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(panel,16,0);
        if (auto theme=static_cast<LvglTheme*>(display->GetTheme())) lv_obj_set_style_text_font(panel,theme->text_font()->font(),0);
        auto text=[&](const std::string& value) { auto label=lv_label_create(panel);lv_obj_set_width(label,LV_PCT(94));lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);lv_label_set_text(label,value.c_str()); };
        text(ready?"音箱账号":"音箱登录");
        if (ready && account_details) {
            text(state.customer_name.empty()?"已绑定集市账号":state.customer_name);
            text(confirm_unbind?"确认退出并解除本台绑定？其他音箱不受影响。":"本台已登录。退出并解绑后可由其他账号扫码使用。");
            if (confirm_unbind) Button(panel,"确认退出并解绑",[](lv_event_t*) { confirm_unbind=false;account_details=false;DeviceAccount::GetInstance().RequestUnbind();DeviceAccount::GetInstance().Render(); });
            else Button(panel,"退出并解绑",[](lv_event_t*) { confirm_unbind=true;DeviceAccount::GetInstance().Render(); });
            Button(panel,"返回对话",[](lv_event_t*) { confirm_unbind=false;account_details=false;DeviceAccount::GetInstance().Render(); });return;
        }
        if (!online) text("网络已断开，请先完成配网或恢复网络");
        else if (!message.empty()) text(message);
        else text(StatusText(state.use_status));
        if (pending) { text("操作结果确认前，本台语音暂停");return; }
        if (!login.verification_uri.empty() && Now()<login.valid_until_ms) {
            auto qr=lv_qrcode_create(panel);lv_qrcode_set_size(qr,260);lv_qrcode_set_dark_color(qr,lv_color_black());lv_qrcode_set_light_color(qr,lv_color_white());
            if (lv_qrcode_update(qr,login.verification_uri.data(),login.verification_uri.size())!=LV_RESULT_OK) text(login.verification_uri);
            text("核对码："+login.user_code);
        } else if (login.status=="authorized" || login.status=="completed") text("扫码已确认，正在完成本台绑定");
        else if (!login.id.empty()) text(login.error=="OWNER_REQUIRED"?"请使用原归属账号扫码；更换账号前先解除本台绑定":"登录已过期或未被允许，请重新扫码");
        if (initialized_ && state.use_status!="admin_disabled" && (state.use_status=="unbound" || state.use_status=="signed_out" || state.use_status=="authorization_required"))
            Button(panel,"重新扫码",[](lv_event_t*) { DeviceAccount::GetInstance().RetryLogin(); });
    });
}

esp_err_t DeviceAccount::ResetUserSettings() {
    // Identity and pending unbind receipts survive local preference resets.
    // No whole-NVS erase, repartition, device deletion or implicit unbind.
    std::set<std::string> names;nvs_iterator_t iterator=nullptr;
    esp_err_t result=nvs_entry_find("nvs",nullptr,NVS_TYPE_ANY,&iterator);
    while (result==ESP_OK) {
        nvs_entry_info_t info;nvs_entry_info(iterator,&info);
        if (std::string(info.namespace_name)!="device_ctrl") names.insert(info.namespace_name);
        result=nvs_entry_next(&iterator);
    }
    nvs_release_iterator(iterator);
    if (result!=ESP_ERR_NVS_NOT_FOUND) return result;
    for (const auto& name:names) {
        nvs_handle_t handle;result=nvs_open(name.c_str(),NVS_READWRITE,&handle);if (result!=ESP_OK) return result;
        result=nvs_erase_all(handle);if (result==ESP_OK) result=nvs_commit(handle);nvs_close(handle);if (result!=ESP_OK) return result;
    }
    return ESP_OK;
}
}
