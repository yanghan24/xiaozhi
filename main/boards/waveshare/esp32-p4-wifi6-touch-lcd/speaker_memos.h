#pragma once
#include <sdkconfig.h>

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "speaker_account/speaker_account.h"

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
    std::vector<Entry> Entries(const SpeakerAccount::MemoAccess& access) const;
    uint32_t Revision() const;
    void Tick();

private:
    using MemoAccess = SpeakerAccount::MemoAccess;
    struct Data {
        int32_t last_id = 0;
        std::array<Entry, kCapacity> entries{};
    };

    struct LegacyData {
        int64_t binding_version;
        Data memos;
    };

    struct AccountData {
        SpeakerAccount::MemoOwner owner;
        Data memos;
    };

    bool LoadFor(const MemoAccess& access);
    bool Save(const Data& data);
    std::vector<Entry> CopyEntries() const;
    std::string Create(const MemoAccess& access, const std::string& content, std::string when);
    std::string Cancel(const MemoAccess& access, int id);
    const Entry* FindDue(std::time_t now) const;
    void Remind(const MemoAccess& access);

    mutable std::mutex mutex_;
    Data data_;
    MemoAccess loaded_access_{};
    uint32_t revision_ = 0;
    bool ready_ = false;
    bool migrated_legacy_pending_ = false;
    bool reminder_pending_ = false;
    std::time_t checked_second_ = 0;
};

#endif
