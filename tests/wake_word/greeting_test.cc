#include <atomic>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

// Hardware and transport boundaries are recorded; the handlers themselves
// are compiled verbatim from main/application.cc by check_greeting.py.
#define ESP_LOGI(...) ((void)0)
#define CONFIG_SEND_WAKE_WORD_DATA 1
enum DeviceState { kDeviceStateIdle, kDeviceStateConnecting, kDeviceStateSpeaking,
                   kDeviceStateListening, kDeviceStateActivating,
                   kDeviceStateWifiConfiguring, kDeviceStateAudioTesting };
enum AbortReason { kAbortReasonWakeWordDetected };
enum PowerSaveLevel { PERFORMANCE };
namespace Lang::Sounds { constexpr auto OGG_POPUP = "popup"; }
std::vector<std::string> events;

struct DeviceControl {
    static DeviceControl& GetInstance() { static DeviceControl instance; return instance; }
    void Refresh() {}
    int GetView() const { return 0; }
    int GetGeneration() const { return 1; }
};
struct Display { void ShowDeviceBinding(int) {} };
struct Board {
    static Board& GetInstance() { static Board instance; return instance; }
    Display* GetDisplay() { static Display display; return &display; }
    void SetPowerSaveLevel(PowerSaveLevel) {}
};
struct IotTelemetryService {
    static IotTelemetryService& GetInstance() { static IotTelemetryService instance; return instance; }
    void ReportWakeWord(const std::string&, const std::string&) {}
};
struct Audio {
    bool stale_playback = false;
    std::string GetLastWakeWord() const { return "你好小智"; }
    void EncodeWakeWord() {}
    void EnableWakeWordDetection(bool) {}
    std::unique_ptr<int> PopPacketFromSendQueue() { return {}; }
    std::unique_ptr<int> PopWakeWordPacket() { return {}; }
    void ResetDecoder() { stale_playback = false; events.push_back("reset"); }
    void PlaySound(const char*, bool) { events.push_back("popup"); }
};
struct Protocol {
    bool connected = true;
    bool can_connect = true;
    bool IsAudioChannelOpened() const { return connected; }
    bool OpenAudioChannel() { return can_connect; }
    void SendStartListening(int) { events.push_back("listen"); }
    void SendWakeWordDetected(const std::string& word) { events.push_back("detect:" + word); }
    void SendAudio(std::unique_ptr<int>) {}
    std::string session_id() const { return "test"; }
};
struct Application {
    Audio audio_service_;
    Protocol transport;
    Protocol* protocol_ = &transport;
    DeviceState state = kDeviceStateIdle;
    bool allowed = true;
    bool play_popup_on_listening_ = false;
    std::atomic<bool> standby_requested_{false};
    bool VoiceAllowed() const { return allowed; }
    DeviceState GetDeviceState() const { return state; }
    bool SetDeviceState(DeviceState next) { state = next; return true; }
    void ScheduleVoice(int, std::function<void()> action) { action(); }
    int GetDefaultListeningMode() const { return 0; }
    void SetListeningMode(int) {
        if (audio_service_.stale_playback) events.push_back("drain-old-answer");
        state = kDeviceStateListening;
        events.push_back("listen");
    }
    void AbortSpeaking(AbortReason) { events.push_back("abort"); }
    void HandleWakeWordDetectedEvent();
    void ContinueWakeWordInvoke(const std::string&);
};

#include "wake_handlers.inc"

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        for (const auto& event : events) std::cerr << "  " << event << '\n';
        std::exit(1);
    }
}

int main() {
    for (auto state : {kDeviceStateIdle, kDeviceStateSpeaking}) {
        Application app;
        app.state = state;
        app.audio_service_.stale_playback = state == kDeviceStateSpeaking;
        events.clear();
        app.HandleWakeWordDetectedEvent();
        int detections = 0;
        bool reset = false;
        for (const auto& event : events) {
            if (event == "reset") reset = true;
            if (event == "detect:你好小智") {
                ++detections;
            }
            if (event == "listen" && state == kDeviceStateSpeaking)
                require(reset, "discard old audio before starting the next question");
            require(event != "drain-old-answer", "wake must not wait for the old answer to finish");
            require(event != "popup", "wake greeting must not also play a popup");
        }
        require(detections == (state == kDeviceStateIdle ? 1 : 0),
                "only an idle wake requests a greeting; interruption is silent");
        require(!app.play_popup_on_listening_, "wake must not schedule a popup");
        require(app.state == kDeviceStateListening, "wake must return to listening");
    }
    for (auto state : {kDeviceStateIdle, kDeviceStateSpeaking}) {
        Application app;
        app.state = state;
        app.allowed = false;
        events.clear();
        app.HandleWakeWordDetectedEvent();
        require(events.empty(), "revoked devices must not request a greeting");
    }
    Application disconnected;
    disconnected.transport.connected = false;
    disconnected.transport.can_connect = false;
    events.clear();
    disconnected.HandleWakeWordDetectedEvent();
    require(events.empty(), "failed connections must not request a greeting");
    require(disconnected.state == kDeviceStateIdle, "failed connections return to idle");
    std::cout << "PASS: idle greeting, silent interruption with next-turn listening, revocation and connection failure\n";
}
