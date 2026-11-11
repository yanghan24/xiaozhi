#ifndef WIFI_BOARD_H
#define WIFI_BOARD_H

#include <esp_event.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <atomic>
#include <mutex>
#include "board.h"

class WifiBoard : public Board {
protected:
    esp_timer_handle_t connect_timer_ = nullptr;
    NetworkEventCallback network_event_callback_ = nullptr;

    virtual std::string GetBoardJson() override;

    /**
     * Handle network event (called from WiFi manager callbacks)
     * @param event The network event type
     * @param data Additional data (e.g., SSID for Connecting/Connected events)
     */
    void OnNetworkEvent(NetworkEvent event, const std::string& data = "");

    /**
     * Start WiFi connection attempt
     */
    void TryWifiConnect(bool boot = false);

    /**
     * Enter WiFi configuration mode
     */
    void StartWifiConfigMode();

    /**
     * WiFi connection timeout callback
     */
    static void OnWifiConnectTimeout(void* arg);

private:
    enum class StationAction : int32_t { Start, Stop, Reconfigure };
    std::atomic<bool> wifi_enabled_{true};
    std::atomic<bool> station_action_pending_{false};
    std::atomic<uint32_t> wifi_session_{0};
    std::atomic<int64_t> connect_deadline_us_{0};
    esp_event_handler_instance_t station_handler_ = nullptr;
    mutable std::mutex wifi_link_mutex_;
    WifiLinkView wifi_link_{WifiLinkState::Connecting, {}, {}};
    void SetWifiLink(WifiLinkState state, const std::string& ssid = {},
                     const std::string& setup_url = {});
    void CancelConnectTimeout();
    bool PostStationAction(StationAction action);
    void RunStationAction(StationAction action);
    void FinishWifiOff();

public:
    WifiBoard();
    virtual ~WifiBoard();
    
    virtual std::string GetBoardType() override;
    
    /**
     * Start network connection asynchronously
     * This function returns immediately. Network events are notified through the callback set by SetNetworkEventCallback().
     */
    virtual void StartNetwork() override;
    
    virtual NetworkInterface* GetNetwork() override;
    virtual void SetNetworkEventCallback(NetworkEventCallback callback) override;
    virtual const char* GetNetworkStateIcon() override;
    WifiLinkView GetWifiLinkView() const override;
    void SetWifiEnabled(bool enabled) override;
    void ConfigureWifi() override;
    virtual void SetPowerSaveLevel(PowerSaveLevel level) override;
    virtual AudioCodec* GetAudioCodec() override { return nullptr; }
    virtual std::string GetDeviceStatusJson() override;
    
    /**
     * Enter WiFi configuration mode (thread-safe, can be called from any task)
     */
    void EnterWifiConfigMode();
    
    /**
     * Check if in WiFi config mode
     */
    bool IsInWifiConfigMode() const;
};

#endif // WIFI_BOARD_H
