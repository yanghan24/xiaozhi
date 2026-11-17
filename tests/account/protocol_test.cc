#include "account_protocol.h"
#include <cassert>
#include <iostream>

int main() {
    const std::string token(43,'x');
    account::Identity identity;
    const std::string registration="{\"version\":1,\"hardware_id\":\"aabbccddeeff\",\"device_id\":\"speaker-fixture\",\"credential\":\""+token+"\",\"ota_url\":\"https://fixture.example/xiaozhi/ota/\",\"control_origin\":\"https://fixture.example\"}";
    assert(account::ParseIdentity(registration,"aabbccddeeff",identity));
    assert(identity.device_id=="speaker-fixture");
    assert(!account::ParseIdentity(registration,"000000000000",identity));assert(identity.credential.empty());
    const std::string original_url="https://fixture.example/xiaozhi/ota/";
    std::string wrong_origin=registration;wrong_origin.replace(wrong_origin.find(original_url),original_url.size(),"https://another.example/xiaozhi/ota/");
    assert(!account::ParseIdentity(wrong_origin,"aabbccddeeff",identity));
    account::State state;
    const auto ready="{\"protocol_version\":2,\"device_id\":\"speaker-fixture\",\"binding_state\":\"bound\",\"permission_version\":7,\"use_status\":\"ready\",\"lease_expires_in_ms\":60000,\"server_time_ms\":100000,\"voice\":{\"protocol_version\":1,\"expires_at_ms\":120000,\"url\":\"wss://fixture.example/xiaozhi/v1/\",\"token\":\""+token+"\"}}";
    assert(account::ParseState(ready,"speaker-fixture",500,state));assert(state.ready);assert(state.valid_until_ms==20500);
    assert(!account::ParseState(ready,"different-device",500,state));assert(!state.ready && state.websocket_token.empty());
    assert(account::ParseState("{\"protocol_version\":2,\"device_id\":\"speaker-fixture\",\"permission_version\":8,\"use_status\":\"unbound\"}","speaker-fixture",500,state));
    assert(!state.ready && state.websocket_token.empty());
    const std::string voice_version="\"protocol_version\":1";
    auto incompatible=ready;auto position=incompatible.find(voice_version);incompatible.replace(position,voice_version.size(),"\"protocol_version\":2");
    assert(!account::ParseState(incompatible,"speaker-fixture",500,state));
    account::Login login;
    const std::string pending="{\"id\":\"12345678-1234-1234-1234-123456789012\",\"status\":\"pending\",\"verification_uri\":\"https://fixture.example/device?user_code=ABCD\",\"user_code\":\"ABCD\",\"expires_at_ms\":2000,\"server_time_ms\":1000}";
    assert(account::ParseLogin(pending,500,login));assert(login.valid_until_ms==1500);assert(login.user_code=="ABCD");
    auto expired=pending;const std::string expires="\"expires_at_ms\":2000";
    expired.replace(expired.find(expires),expires.size(),"\"expires_at_ms\":900");
    assert(!account::ParseLogin(expired,500,login));assert(login.verification_uri.empty());
    assert(account::ParseLogin("{\"id\":\"12345678-1234-1234-1234-123456789012\",\"status\":\"denied\",\"error_code\":\"OWNER_REQUIRED\"}",500,login));
    assert(login.verification_uri.empty() && login.user_code.empty());
    std::cout<<"identity, same-origin OTA, device scope, voice protocol, lease, QR expiry and denied login passed\n";
}
