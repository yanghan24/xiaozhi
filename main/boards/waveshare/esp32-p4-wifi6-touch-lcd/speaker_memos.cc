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
constexpr char kDataKey[] = "entries_v1";
constexpr std::time_t kEarliestClock = 1704067200;
constexpr int kReminderGraceSeconds = 600;
constexpr int kPageSize = 4;
static_assert(SpeakerMemos::kCapacity % kPageSize == 0);

int64_t Binding() {
    return SpeakerAccount::Get().MemoBindingVersion();
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
    Load();
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
            return Create(Binding(), properties["content"].value<std::string>(),
                          properties["when"].value<std::string>());
        });
    server.AddTool("self.memo.list",
        "Read memos on this speaker and its current local device_time. Optionally filter by date "
        "(YYYY-MM-DD). Entries include their ID and reminder status. Use the next_page value if nonzero.",
        PropertyList({date, Property("page", kPropertyTypeInteger, 1, 1,
                                     kCapacity / kPageSize)}),
        [this](const PropertyList& properties) -> ToolResult {
            const auto binding = Binding();
            if (binding <= 0) {
                return std::unexpected("请先在音箱上登录账号");
            }
            if (!ready_) {
                return std::unexpected("本机记事存储暂不可用");
            }
            const auto date = properties["date"].value<std::string>();
            if (!date.empty() && !ParseTime(date)) {
                return std::unexpected("日期无效，请使用 YYYY-MM-DD");
            }
            auto entries = Entries(binding);
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
            return Cancel(Binding(), properties["id"].value<int>());
        });
}

void SpeakerMemos::Load() {
    std::lock_guard lock(mutex_);
    nvs_handle_t handle;
    auto error = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        ready_ = true;
        return;
    }
    if (error != ESP_OK) {
        return;
    }
    size_t size = sizeof(data_);
    error = nvs_get_blob(handle, kDataKey, &data_, &size);
    nvs_close(handle);
    ready_ = error == ESP_ERR_NVS_NOT_FOUND || (error == ESP_OK && size == sizeof(data_));
}

bool SpeakerMemos::Save(const Data& data) {
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }
    auto error = nvs_set_blob(handle, kDataKey, &data, sizeof(data));
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    if (error != ESP_OK) {
        return false;
    }
    data_ = data;
    ++revision_;
    return true;
}

std::string SpeakerMemos::Create(int64_t binding, const std::string& content, std::string when) {
    if (binding <= 0) {
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
    if (!ready_) {
        return "未保存，本机记事存储暂不可用";
    }
    std::unique_ptr<Data> next(new (std::nothrow) Data);
    if (!next) {
        return "未保存，本机暂时无法处理记事";
    }
    if (data_.binding == binding) {
        *next = data_;
    }
    next->binding = binding;
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

std::string SpeakerMemos::Cancel(int64_t binding, int id) {
    if (binding <= 0) {
        return "未取消，请先在音箱上登录账号";
    }
    std::lock_guard lock(mutex_);
    if (!ready_) {
        return "未取消，本机记事存储暂不可用";
    }
    if (data_.binding == binding) {
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
    }
    return "未找到该记事，请先查询本机记事编号";
}

std::vector<SpeakerMemos::Entry> SpeakerMemos::Entries(int64_t binding) const {
    std::lock_guard lock(mutex_);
    std::vector<Entry> entries;
    if (ready_ && binding > 0 && binding == data_.binding) {
        std::copy_if(data_.entries.begin(), data_.entries.end(), std::back_inserter(entries),
                     [](const Entry& entry) { return entry.id != 0; });
        std::sort(entries.begin(), entries.end(), [](const Entry& first, const Entry& second) {
            const int order = std::strcmp(first.when, second.when);
            return order == 0 ? first.id < second.id : order < 0;
        });
    }
    return entries;
}

uint32_t SpeakerMemos::Revision() const {
    std::lock_guard lock(mutex_);
    return revision_;
}

const SpeakerMemos::Entry* SpeakerMemos::FindDue(int64_t binding, std::time_t now) const {
    if (!ready_ || binding <= 0 || binding != data_.binding || !now) {
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
    const auto binding = Binding();
    const auto now = SpeakerClock::Now();
    if (binding <= 0 || !now) {
        return;
    }
    {
        std::lock_guard lock(mutex_);
        if (reminder_pending_ || checked_second_ == now) {
            return;
        }
        checked_second_ = now;
        if (!FindDue(binding, now)) {
            return;
        }
        reminder_pending_ = true;
    }
    app.Schedule([this] { Remind(); });
}

void SpeakerMemos::Remind() {
    auto& app = Application::GetInstance();
    auto& audio = app.GetAudioService();
    const bool idle = app.GetDeviceState() == kDeviceStateIdle && audio.IsPlaybackIdle() &&
        !audio.IsLocalCaptureRunning();
    const auto binding = Binding();
    std::string notice;
    {
        std::lock_guard lock(mutex_);
        reminder_pending_ = false;
        const auto* entry = idle ? FindDue(binding, SpeakerClock::Now()) : nullptr;
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
    if (Binding() == binding) {
        Board::GetInstance().GetDisplay()->ShowNotification(notice.c_str(), 6000);
        app.PlaySound(Lang::Sounds::OGG_POPUP);
    }
}

#endif
