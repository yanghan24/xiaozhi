#ifndef IOT_TELEMETRY_SERVICE_H
#define IOT_TELEMETRY_SERVICE_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

struct cJSON;

class IotTelemetryService {
public:
    static IotTelemetryService& GetInstance();

    void Start();
    void SetNetworkConnected(bool connected);
    void ReportWakeWord(const std::string& word, const std::string& session_id = "");
    void ReportQuestion(const std::string& text, const std::string& session_id);
    void ReportTtsState(const std::string& state, const std::string& text,
                        const std::string& session_id);
    void ReportVoiceConnectionChanged(bool connected, const std::string& transport,
                                      const std::string& session_id,
                                      const std::string& reason = "");

private:
    struct PendingMessage {
        std::string path;
        std::string message_id;
        std::string body;
        bool heartbeat = false;
        int retry_step = 0;
        int64_t next_attempt_ms = 0;
    };

    enum class SendResult {
        kAccepted,
        kRetry,
        kAuthFailure,
        kRejected,
    };

    IotTelemetryService() = default;
    ~IotTelemetryService() = default;
    IotTelemetryService(const IotTelemetryService&) = delete;
    IotTelemetryService& operator=(const IotTelemetryService&) = delete;

    static void TaskEntry(void* arg);
    void Run();
    void WakeTask();
    void QueueEvent(const char* event_id, cJSON* data, int64_t timestamp_ms);
    void QueueHeartbeatIfDue(int64_t now_ms);
    PendingMessage BuildHeartbeat(int64_t timestamp_ms);
    SendResult Send(const PendingMessage& message);
    std::string MakeMessageId(const char* kind, int64_t timestamp_ms);
    void LoadPendingEvents();
    void PersistPendingEvents();

    bool enabled_ = false;
    std::string base_url_;
    std::string product_id_;
    std::string device_id_;
    std::string token_;
    int heartbeat_seconds_ = 30;

    std::atomic<bool> started_{false};
    std::atomic<bool> network_connected_{false};
    std::atomic<uint32_t> message_counter_{0};
    std::atomic<bool> event_queue_dirty_{false};
    TaskHandle_t task_handle_ = nullptr;

    std::mutex mutex_;
    std::deque<PendingMessage> queue_;
    std::string last_question_;
    std::string last_answer_;
    std::string current_turn_id_;
    std::string pending_answer_;
    bool pending_answer_truncated_ = false;
    bool voice_connected_ = false;
    std::string voice_transport_;
    std::string voice_session_id_;
    std::string tts_state_;
    int64_t next_heartbeat_ms_ = 0;
};

#endif  // IOT_TELEMETRY_SERVICE_H
