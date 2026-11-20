#pragma once
#include <sdkconfig.h>

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include <array>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

class SpeakerMemos {
public:
    static constexpr size_t kCapacity = 12;
    static constexpr size_t kContentBytes = 120;

    struct Entry {
        int32_t id = 0;
        char when[17] = {};
        char content[kContentBytes + 1] = {};
        bool reminded = false;
    };

    static SpeakerMemos& Get();
    void InitializeTools();
    std::vector<Entry> Entries(int64_t binding) const;
    uint32_t Revision() const;
    void Tick();

private:
    struct Data {
        int64_t binding = 0;
        int32_t last_id = 0;
        std::array<Entry, kCapacity> entries{};
    };

    void Load();
    bool Save(const Data& data);
    std::string Create(int64_t binding, const std::string& content, std::string when);
    std::string Cancel(int64_t binding, int id);
    const Entry* FindDue(int64_t binding, std::time_t now) const;
    void Remind();

    mutable std::mutex mutex_;
    Data data_;
    uint32_t revision_ = 0;
    bool ready_ = false;
    bool reminder_pending_ = false;
    std::time_t checked_second_ = 0;
};

#endif
