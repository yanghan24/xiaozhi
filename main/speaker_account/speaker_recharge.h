#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstdint>
#include <mutex>
#include <string>

class SpeakerRecharge {
public:
    enum class Page { Inactive, Loading, Consent, Checkout, Waiting, Pending, Paid, Failed };
    struct View {
        Page page = Page::Inactive;
        std::string url;
        std::string code;
        std::string message;
        bool operator==(const View&) const = default;
    };

    static SpeakerRecharge& Get();
    View GetView() const;
    void Open();
    void Close();

private:
    void Run();
    void Checkout(uint32_t generation, int64_t permission);
    bool Current(uint32_t generation, int64_t permission) const;
    bool Publish(uint32_t generation, View view);

    mutable std::mutex mutex_;
    View view_;
    uint32_t generation_ = 0;
    TaskHandle_t worker_ = nullptr;
};

void RenderSpeakerRecharge();
