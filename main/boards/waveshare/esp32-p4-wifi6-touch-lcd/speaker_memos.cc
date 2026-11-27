#include "speaker_memos.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "application.h"
#include "assets/lang_config.h"
#include "board.h"
#include "display.h"
#include "mcp_server.h"
#include "speaker_account/speaker_account.h"
#include "speaker_account/speaker_clock.h"

#include <nvs.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <new>

namespace {

constexpr char kNamespace[] = "speaker_memos";
constexpr char kLegacyKey[] = "entries_v1";
constexpr std::time_t kEarliestClock = 1704067200;
constexpr int kReminderGraceSeconds = 600;
constexpr int kPageSize = 4;
static_assert(SpeakerMemos::kCapacity % kPageSize == 0);

SpeakerAccount::MemoAccess CurrentAccess() {
    return SpeakerAccount::Get().GetMemoAccess();
}

std::array<char, 16> StorageKey(const SpeakerAccount::MemoOwner& owner) {
    // NVS limits keys to 15 characters. The stored record also carries the complete owner ID.
    std::array<char, 16> key{};
    std::copy_n(owner.begin(), key.size() - 1, key.begin());
    return key;
}

std::string FormatTime(std::time_t time, const char* format = "%Y-%m-%d %H:%M") {
    std::tm local{};
    char text[24];
    if (time < kEarliestClock || !localtime_r(&time, &local) ||
        !std::strftime(text, sizeof(text), format, &local)) {
        return {};
    }
    return text;
}

// The device clock uses the same local time as the standby screen and calendar.
std::time_t ParseTime(const std::string& text) {
    const bool date_only = text.size() == 10;
    if (!date_only && text.size() != 16) {
        return 0;
    }
    std::tm local{};
    const char* format = date_only ? "%Y-%m-%d" : "%Y-%m-%d %H:%M";
    const char* end = strptime(text.c_str(), format, &local);
    if (!end || *end != '\0' || local.tm_year < 124 || local.tm_year > 199) {
        return 0;
    }
    local.tm_isdst = -1;
    const auto time = std::mktime(&local);
    return FormatTime(time, format) == text ? time : 0;
}

}  // namespace

SpeakerMemos& SpeakerMemos::Get() {
    static SpeakerMemos memos;
    return memos;
}

void SpeakerMemos::InitializeTools() {
    auto& server = McpServer::GetInstance();
    auto content = Property("content", kPropertyTypeString);
    content.SetMaxLength(kContentBytes);
    auto when = Property("when", kPropertyTypeString, std::string{});
    when.SetMaxLength(16);
    auto date = Property("date", kPropertyTypeString, std::string{});
    date.SetMaxLength(10);
    server.AddTool("self.memo.create",
        "Save a memo or one-time reminder on this speaker when the user asks to remember something. "
        "Use YYYY-MM-DD HH:MM in device local time for a timed reminder, YYYY-MM-DD for a date-only memo, "
        "or omit when for a memo under today's date. For relative times such as tomorrow at 9, first "
        "use the memo list tool to read device_time. Ask when the requested time is ambiguous. "
        "Only say it was saved when the tool reports success.",
        PropertyList({content, when}), [this](const PropertyList& properties) -> ReturnValue {
            return Create(CurrentAccess(), properties["content"].value<std::string>(),
                          properties["when"].value<std::string>());
        });
    server.AddTool("self.memo.list",
        "Read memos on this speaker and its current local device_time. Optionally filter by date "
        "(YYYY-MM-DD). Entries include their ID and reminder status. Use the next_page value if nonzero.",
        PropertyList({date, Property("page", kPropertyTypeInteger, 1, 1,
                                     kCapacity / kPageSize)}),
        [this](const PropertyList& properties) -> ToolResult {
            const auto access = CurrentAccess();
            if (access.binding_version <= 0) {
                return std::unexpected("请先在音箱上登录账号");
            }
            const auto date = properties["date"].value<std::string>();
            if (!date.empty() && !ParseTime(date)) {
                return std::unexpected("日期无效，请使用 YYYY-MM-DD");
            }
            std::vector<Entry> entries;
            {
                std::lock_guard lock(mutex_);
                if (!LoadFor(access)) {
                    return std::unexpected("本机记事存储暂不可用");
                }
                entries = CopyEntries();
            }
            if (!date.empty()) {
                std::erase_if(entries, [&date](const Entry& entry) {
                    return !std::string_view(entry.when).starts_with(date);
                });
            }
            const size_t page = properties["page"].value<int>();
            const size_t next = page * kPageSize;
            const auto now = SpeakerClock::Now();
            CJsonUniquePtr result(cJSON_CreateObject());
            if (!result) {
                return std::unexpected("无法读取本机记事");
            }
            cJSON_AddStringToObject(result.get(), "device_time", FormatTime(now).c_str());
            cJSON_AddNumberToObject(result.get(), "next_page", next < entries.size() ? page + 1 : 0);
            auto* items = cJSON_AddArrayToObject(result.get(), "memos");
            if (!items) {
                return std::unexpected("无法读取本机记事");
            }
            for (size_t i = next - kPageSize; i < std::min(next, entries.size()); ++i) {
                const auto& entry = entries[i];
                auto* item = cJSON_CreateObject();
                if (!cJSON_AddItemToArray(items, item)) {
                    cJSON_Delete(item);
                    return std::unexpected("无法读取本机记事");
                }
                cJSON_AddNumberToObject(item, "id", entry.id);
                cJSON_AddStringToObject(item, "when", entry.when);
                cJSON_AddStringToObject(item, "content", entry.content);
                if (std::strlen(entry.when) == 16) {
                    const auto due = ParseTime(entry.when);
                    cJSON_AddStringToObject(item, "reminder", entry.reminded ? "reminded" :
                        due + kReminderGraceSeconds < now ? "missed" : "pending");
                }
            }
            return result.release();
        });
    server.AddTool("self.memo.cancel",
        "Remove a memo or reminder from this speaker after the user asks to cancel it. "
        "First use the memo list tool to find its exact ID. Ask which one if multiple entries match.",
        PropertyList({Property("id", kPropertyTypeInteger, 1, std::numeric_limits<int>::max())}),
        [this](const PropertyList& properties) -> ReturnValue {
            return Cancel(CurrentAccess(), properties["id"].value<int>());
        });
}

bool SpeakerMemos::LoadFor(const MemoAccess& access) {
    if (access.binding_version <= 0 || CurrentAccess() != access) {
        return false;
    }
    if (access == loaded_access_) {
        return ready_;
    }
    loaded_access_ = access;
    data_ = {};
    migrated_legacy_pending_ = false;
    ++revision_;
    nvs_handle_t handle;
    auto error = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (error != ESP_OK) {
        ready_ = error == ESP_ERR_NVS_NOT_FOUND;
        return ready_;
    }
    error = ESP_ERR_NVS_NOT_FOUND;
    if (access.owner.front() != '\0') {
        const auto key = StorageKey(access.owner);
        auto record = std::unique_ptr<AccountData>(new (std::nothrow) AccountData{});
        if (record) {
            size_t size = sizeof(AccountData);
            error = nvs_get_blob(handle, key.data(), record.get(), &size);
            constexpr size_t header = offsetof(AccountData, memos) + offsetof(Data, entries);
            if (error == ESP_OK) {
                if (size < header || (size - header) % sizeof(Entry) != 0 || record->owner != access.owner) {
                    error = ESP_ERR_INVALID_STATE;
                } else {
                    data_ = record->memos;
                }
            }
        } else {
            error = ESP_ERR_NO_MEM;
        }
    }
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        auto legacy = std::unique_ptr<LegacyData>(new (std::nothrow) LegacyData{});
        if (legacy) {
            size_t size = sizeof(LegacyData);
            error = nvs_get_blob(handle, kLegacyKey, legacy.get(), &size);
            if (error == ESP_OK) {
                if (size != sizeof(LegacyData)) {
                    error = ESP_ERR_INVALID_STATE;
                } else if (legacy->binding_version == access.binding_version) {
                    data_ = legacy->memos;
                    migrated_legacy_pending_ = access.owner.front() != '\0';
                }
            }
        } else {
            error = ESP_ERR_NO_MEM;
        }
    }
    nvs_close(handle);
    ready_ = error == ESP_ERR_NVS_NOT_FOUND || error == ESP_OK;
    if (migrated_legacy_pending_) {
        Save(data_);
    }
    return ready_;
}

bool SpeakerMemos::Save(const Data& data) {
    if (CurrentAccess() != loaded_access_) {
        return false;
    }
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }
    esp_err_t error = ESP_ERR_NO_MEM;
    if (loaded_access_.owner.front() == '\0') {
        auto legacy = std::unique_ptr<LegacyData>(new (std::nothrow) LegacyData{loaded_access_.binding_version, data});
        if (legacy) {
            error = nvs_set_blob(handle, kLegacyKey, legacy.get(), sizeof(LegacyData));
        }
    } else {
        auto record = std::unique_ptr<AccountData>(new (std::nothrow) AccountData{});
        if (record) {
            record->owner = loaded_access_.owner;
            record->memos.last_id = data.last_id;
            const auto end = std::copy_if(data.entries.begin(), data.entries.end(), record->memos.entries.begin(),
                                          [](const Entry& entry) { return entry.id != 0; });
            const size_t count = end - record->memos.entries.begin();
            const auto key = StorageKey(loaded_access_.owner);
            const size_t size = offsetof(AccountData, memos) + offsetof(Data, entries) + count * sizeof(Entry);
            error = nvs_set_blob(handle, key.data(), record.get(), size);
        }
    }
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    if (error == ESP_OK && migrated_legacy_pending_) {
        // Keep the old record until the new account record is committed.
        const auto erased = nvs_erase_key(handle, kLegacyKey);
        migrated_legacy_pending_ = erased != ESP_ERR_NVS_NOT_FOUND &&
            (erased != ESP_OK || nvs_commit(handle) != ESP_OK);
    }
    nvs_close(handle);
    if (error != ESP_OK) {
        return false;
    }
    data_ = data;
    ++revision_;
    return true;
}

std::string SpeakerMemos::Create(const MemoAccess& access, const std::string& content, std::string when) {
    if (access.binding_version <= 0) {
        return "未保存，请先在音箱上登录账号";
    }
    if (content.empty() || content.size() > kContentBytes ||
        std::any_of(content.begin(), content.end(), [](unsigned char ch) { return ch < 32; }) ||
        content.find_first_not_of(' ') == std::string::npos) {
        return "未保存，请提供简短的记事内容";
    }
    const auto now = SpeakerClock::Now();
    if (!now) {
        return "未保存，音箱时间尚未同步";
    }
    if (when.empty()) {
        when = FormatTime(now, "%Y-%m-%d");
    }
    const auto date = ParseTime(when);
    if (!date) {
        return "未保存，请提供有效日期";
    }
    std::lock_guard lock(mutex_);
    if (!LoadFor(access)) {
        return "未保存，本机记事存储暂不可用";
    }
    std::unique_ptr<Data> next(new (std::nothrow) Data(data_));
    if (!next) {
        return "未保存，本机暂时无法处理记事";
    }
    auto slot = next->entries.end();
    for (auto it = next->entries.begin(); it != next->entries.end(); ++it) {
        if (it->id != 0 && when == it->when && content == it->content) {
            return "已保存，编号 " + std::to_string(it->id) + "，日期 " + when;
        }
        if (it->id == 0 && slot == next->entries.end()) {
            slot = it;
        }
    }
    if (when.size() == 16 && date <= now) {
        return "未保存，请提供尚未到达的提醒时间";
    }
    if (slot == next->entries.end() || next->last_id == std::numeric_limits<int32_t>::max()) {
        return "未保存，本机记事已满，请先取消不需要的条目";
    }
    slot->id = ++next->last_id;
    std::strcpy(slot->when, when.c_str());
    std::strcpy(slot->content, content.c_str());
    if (!Save(*next)) {
        return "未保存，本机记事存储写入失败";
    }
    return "已保存，编号 " + std::to_string(slot->id) + "，日期 " + when;
}

std::string SpeakerMemos::Cancel(const MemoAccess& access, int id) {
    if (access.binding_version <= 0) {
        return "未取消，请先在音箱上登录账号";
    }
    std::lock_guard lock(mutex_);
    if (!LoadFor(access)) {
        return "未取消，本机记事存储暂不可用";
    }
    const auto entry = std::find_if(data_.entries.begin(), data_.entries.end(), [id](const Entry& item) {
        return item.id == id;
    });
    if (entry != data_.entries.end()) {
        std::unique_ptr<Data> next(new (std::nothrow) Data(data_));
        if (!next) {
            return "未取消，本机暂时无法处理记事";
        }
        next->entries[entry - data_.entries.begin()] = {};
        return Save(*next) ? "已取消记事" : "未取消，本机记事存储写入失败";
    }
    return "未找到该记事，请先查询本机记事编号";
}

std::vector<SpeakerMemos::Entry> SpeakerMemos::Entries(const MemoAccess& access) const {
    std::lock_guard lock(mutex_);
    if (!ready_ || access != loaded_access_ || CurrentAccess() != access) {
        return {};
    }
    return CopyEntries();
}

std::vector<SpeakerMemos::Entry> SpeakerMemos::CopyEntries() const {
    std::vector<Entry> entries;
    std::copy_if(data_.entries.begin(), data_.entries.end(), std::back_inserter(entries),
                 [](const Entry& entry) { return entry.id != 0; });
    std::sort(entries.begin(), entries.end(), [](const Entry& first, const Entry& second) {
        const int order = std::strcmp(first.when, second.when);
        return order == 0 ? first.id < second.id : order < 0;
    });
    return entries;
}

uint32_t SpeakerMemos::Revision() const {
    std::lock_guard lock(mutex_);
    return revision_;
}

const SpeakerMemos::Entry* SpeakerMemos::FindDue(std::time_t now) const {
    if (!ready_ || !now) {
        return nullptr;
    }
    const Entry* earliest = nullptr;
    for (const auto& entry : data_.entries) {
        if (entry.id != 0 && !entry.reminded && std::strlen(entry.when) == 16) {
            const auto due = ParseTime(entry.when);
            if (due <= now && now - due <= kReminderGraceSeconds &&
                (!earliest || std::strcmp(entry.when, earliest->when) < 0)) {
                earliest = &entry;
            }
        }
    }
    return earliest;
}

void SpeakerMemos::Tick() {
    auto& app = Application::GetInstance();
    if (app.GetDeviceState() != kDeviceStateIdle) {
        return;
    }
    const auto access = CurrentAccess();
    const auto now = SpeakerClock::Now();
    if (access.binding_version <= 0) {
        return;
    }
    {
        std::lock_guard lock(mutex_);
        if (reminder_pending_) {
            return;
        }
        if (access == loaded_access_) {
            if (checked_second_ == now) {
                return;
            }
            checked_second_ = now;
            if (!FindDue(now)) {
                return;
            }
        }
        reminder_pending_ = true;
    }
    app.Schedule([this, access] { Remind(access); });
}

void SpeakerMemos::Remind(const MemoAccess& access) {
    auto& app = Application::GetInstance();
    auto& audio = app.GetAudioService();
    const bool idle = app.GetDeviceState() == kDeviceStateIdle && audio.IsPlaybackIdle() &&
        !audio.IsLocalCaptureRunning();
    std::string notice;
    {
        std::lock_guard lock(mutex_);
        reminder_pending_ = false;
        if (!LoadFor(access)) {
            return;
        }
        checked_second_ = SpeakerClock::Now();
        const auto* entry = idle ? FindDue(checked_second_) : nullptr;
        if (!entry) {
            return;
        }
        std::unique_ptr<Data> next(new (std::nothrow) Data(data_));
        if (!next) {
            return;
        }
        next->entries[entry - data_.entries.data()].reminded = true;
        notice = std::string("日程提醒  ") + entry->content;
        if (!Save(*next)) {
            return;
        }
    }
    if (CurrentAccess() == access) {
        Board::GetInstance().GetDisplay()->ShowNotification(notice.c_str(), 6000);
        app.PlaySound(Lang::Sounds::OGG_POPUP);
    }
}

#endif
