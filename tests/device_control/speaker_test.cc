#include "speaker_model.h"
#include <cassert>
#include <string>

namespace {
std::string State(int pv, bool ready, int server = 100000, int binding = 1) {
    std::string voice = ready ? "{\"url\":\"wss://voice.example/xiaozhi/v1/\",\"token\":\"" + std::string(43,'x') +
        "\",\"protocol_version\":1,\"permission_version\":" + std::to_string(pv) + ",\"expires_at_ms\":400000}" : "null";
    return "{\"protocol_version\":2,\"device_id\":\"speaker-1\",\"binding_version\":" + std::to_string(binding) +
        ",\"permission_version\":" + std::to_string(pv) + ",\"binding_state\":\"" + (ready ? "bound" : "unbound") +
        "\",\"use_status\":\"" + (ready ? "ready" : "unbound") + "\",\"voice\":" + voice +
        ",\"lease_expires_in_ms\":" + (ready ? "60000" : "0") + ",\"server_time_ms\":" + std::to_string(server) +
        ",\"login_id\":null,\"customer_name\":null}";
}
std::string Login(const std::string& status, int expires = 200000) {
    return "{\"id\":\"12345678-1234-1234-1234-123456789012\",\"status\":\"" + status +
        "\",\"verification_uri\":\"https://aimarket.hncig.cn/activate?user_code=ABCD-EFGH\",\"user_code\":\"ABCD-EFGH\"," +
        "\"expires_at_ms\":" + std::to_string(expires) + ",\"server_time_ms\":100000,\"error_code\":null}";
}
}
int main() {
    using namespace device_control;
    SpeakerModel model("speaker-1");
    assert(model.State(State(1,false),1000,100) == ApplyResult::Applied);
    assert(model.CanLogin() && !model.Allowed(1000));
    assert(model.Login(Login("pending"),1000,100) == ApplyResult::Applied);
    assert(model.View().user_code == "ABCD-EFGH");
    assert(model.View().qr_deadline_ms == 100900);
    assert(!model.CanLogin());
    assert(model.Tick(100900) && model.View().verification_uri.empty());
    model.RetryLogin(); assert(model.CanLogin());
    assert(model.Login(Login("denied"),101000,100) == ApplyResult::Applied);
    assert(!model.CanLogin() && model.View().verification_uri.empty());
    model.RetryLogin(); assert(model.CanLogin());
    auto invalid = State(1,false); invalid.insert(1,"\"protocol_version\":1,");
    assert(model.State(invalid,2000,10) == ApplyResult::Invalid); // duplicate field / old protocol
    assert(model.State(State(2,true),2000,100) == ApplyResult::Applied);
    auto generation = model.View().generation;
    DeviceVoiceConfig voice;
    assert(model.Voice(2000,voice) && voice.device_id == "speaker-1");
    assert(model.State(State(2,true,101000),3000,100) == ApplyResult::Applied);
    assert(model.View().generation == generation); // ticket/heartbeat refresh never stops a reply
    assert(model.State(State(1,false,102000),4000,100) == ApplyResult::Stale);
    assert(model.Tick(62900)); assert(!model.Allowed(62900));
    assert(model.View().generation > generation);
    assert(model.State(State(3,false,103000,2),64000,100) == ApplyResult::Applied);
    assert(!model.Allowed(64000) && model.CanLogin());
    invalid = State(4,true,104000,3); invalid.replace(invalid.find("wss://"),6,"ws://");
    assert(model.State(invalid,65000,10) == ApplyResult::Invalid);
}
