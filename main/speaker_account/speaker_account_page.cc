#include "speaker_account_page.h"
#include "speaker_ui_widgets.h"

#include "application.h"
#include "board.h"
#include "display.h"
#include <qrcode_wrapper.h>

using namespace SpeakerUi;

namespace {
using Page = SpeakerAccount::Page;
SpeakerAccountPage* account_page = nullptr;

const char* Description(Page page) {
    switch (page) {
        case Page::Connecting: return "正在加载";
        case Page::Unprovisioned: return "设备身份尚未交付";
        case Page::Scan: return "用手机扫描二维码并核对授权码";
        case Page::Retry: return "授权未完成，请重新获取二维码";
        case Page::Disabled: return "当前无法使用，请联系管理员";
        case Page::Ready: return "已绑定";
        case Page::Offline: return "连接中断，正在恢复";
    }
    return "";
}

bool RenderQr(lv_obj_t* parent, const std::string& url, lv_draw_buf_t*& buffer) {
    constexpr int side = 240;
    constexpr int quiet_zone = 4;
    qrcode_wrapper_config_t config{
        .display_func = [](qrcode_wrapper_handle_t code, void* output) {
            const int modules = qrcode_wrapper_get_size(code);
            const int scale = side / (modules + 2 * quiet_zone);
            const int begin = (side - modules * scale) / 2;
            auto* buffer = lv_draw_buf_create(side, side, LV_COLOR_FORMAT_I1, LV_STRIDE_AUTO);
            if (!buffer || scale <= 0) {
                if (buffer) {
                    lv_draw_buf_destroy(buffer);
                }
                return;
            }
            lv_draw_buf_clear(buffer, nullptr);
            lv_draw_buf_set_palette(buffer, 0, lv_color_to_32(lv_color_white(), LV_OPA_COVER));
            lv_draw_buf_set_palette(buffer, 1, lv_color_to_32(lv_color_black(), LV_OPA_COVER));
            const int end = begin + modules * scale;
            for (int y = begin; y < end; ++y) {
                auto* row = static_cast<uint8_t*>(lv_draw_buf_goto_xy(buffer, 0, y));
                for (int x = begin; x < end; ++x) {
                    if (qrcode_wrapper_get_module(code, (x - begin) / scale, (y - begin) / scale)) {
                        row[x / 8] |= 0x80 >> (x % 8);
                    }
                }
            }
            *static_cast<lv_draw_buf_t**>(output) = buffer;
        },
        .max_qrcode_version = 40,
        .qrcode_ecc_level = QRCODE_WRAPPER_ECC_LOW,
        .user_data = &buffer,
    };
    if (qrcode_wrapper_generate(&config, url.c_str()) != ESP_OK || !buffer) {
        return false;
    }
    auto* canvas = lv_canvas_create(parent);
    lv_canvas_set_draw_buf(canvas, buffer);
    lv_obj_set_pos(canvas, 84, 123);
    return true;
}


}  // namespace

void SpeakerAccountPage::Create(lv_obj_t* apps, lv_obj_t* system, lv_event_cb_t back, void* context) {
    apps_ = apps;
    system_ = system;
    back_ = back;
    back_context_ = context;
    root_ = Panel(apps);
    view_ = SpeakerAccount::Get().GetView();
    account_page = this;
}

void SpeakerAccountPage::Destroy() {
    account_page = nullptr;
    if (root_) {
        lv_obj_delete(root_);
        root_ = nullptr;
    }
    if (qr_buffer_) {
        lv_draw_buf_destroy(qr_buffer_);
        qr_buffer_ = nullptr;
    }
}

void SpeakerAccountPage::Show() {
    requested_ = true;
    Render();
}

void SpeakerAccountPage::Hide() {
    requested_ = false;
    confirm_unbind_ = false;
    Render();
}

void SpeakerAccountPage::SetPromptsAllowed(bool allowed) {
    if (prompts_allowed_ != allowed) {
        prompts_allowed_ = allowed;
        Render();
    }
}

void SpeakerAccountPage::Apply(const SpeakerAccount::View& view) {
    if (view_ != view) {
        const bool reset_confirmation = view_.page != view.page || view_.customer != view.customer ||
                                        !view.can_unbind || view.unbind_pending;
        if (reset_confirmation) {
            confirm_unbind_ = false;
        }
        view_ = view;
        Render();
    }
}

bool SpeakerAccountPage::PromptVisible() const {
    return prompts_allowed_ && (view_.page == Page::Scan || view_.page == Page::Retry ||
                                view_.page == Page::Unprovisioned || view_.unbind_pending);
}

void SpeakerAccountPage::Render() {
    lv_obj_clean(root_);
    if (qr_buffer_) {
        lv_draw_buf_destroy(qr_buffer_);
        qr_buffer_ = nullptr;
    }
    const bool prompt = PromptVisible();
    auto* parent = prompt ? system_ : apps_;
    if (lv_obj_get_parent(root_) != parent) {
        lv_obj_set_parent(root_, parent);
    }
    lv_obj_set_flag(root_, LV_OBJ_FLAG_CLICKABLE, prompt);
    lv_obj_set_flag(root_, LV_OBJ_FLAG_HIDDEN, !prompt && !requested_);
    if (prompt || requested_) {
        RenderContent(prompt);
    }
}

void SpeakerAccountPage::RenderContent(bool prompt) {
    const bool scan = view_.page == Page::Scan || view_.page == Page::Retry;
    Header(root_, scan ? "扫码登录" : "账号", prompt ? nullptr : back_, back_context_);
    auto* state = Label(root_, view_.unbind_pending ? "正在解除绑定" : Description(view_.page), kCyan);
    lv_obj_align(state, LV_ALIGN_CENTER, 0, -18);
    if (view_.unbind_pending) {
        return;
    }

    if (view_.page == Page::Scan && RenderQr(root_, view_.qr_url, qr_buffer_)) {
        lv_obj_set_width(state, 346);
        lv_obj_set_style_text_align(state, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(state, LV_ALIGN_CENTER, 171, -42);
        auto* code = Label(root_, view_.user_code.c_str());
        Scale(code, 330);
        lv_obj_align(code, LV_ALIGN_CENTER, 171, 42);
        return;
    }
    if (scan) {
        if (view_.page == Page::Scan) {
            UpdateLabel(state, "二维码生成失败，请重新获取");
        }
        auto* retry = Button(root_, "重新获取", [](lv_event_t*) {
            SpeakerAccount::Get().RefreshCode();
        }, nullptr, kCyan, kDeep);
        lv_obj_set_size(retry, 225, 70);
        lv_obj_align(retry, LV_ALIGN_CENTER, 0, 90);
        return;
    }
    if (view_.page == Page::Unprovisioned) {
        lv_obj_align(Label(root_, SpeakerAccount::Get().hardware_id().c_str(), kMuted),
                     LV_ALIGN_CENTER, 0, 48);
        return;
    }
    if (view_.page != Page::Ready && view_.customer.empty()) {
        return;
    }

    auto* avatar = Surface(root_, 217, 158, 132, 132);
    lv_obj_set_style_radius(avatar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(avatar, lv_color_hex(kCard), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(avatar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_center(Icon(avatar, MATERIAL_SYMBOLS_PERSON, kCyan, 640));
    auto* name = Label(root_, view_.customer.empty() ? "已登录" : view_.customer.c_str());
    lv_obj_set_width(name, 257);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    Scale(name, 352);
    lv_obj_set_style_transform_pivot_x(name, 0, LV_PART_MAIN);
    lv_obj_set_pos(name, 382, 167);
    lv_obj_align(state, LV_ALIGN_TOP_LEFT, 382, 224);
    if (view_.can_unbind) {
        auto* button = Button(root_, confirm_unbind_ ? "确认解绑" : "解除绑定", OnUnbind, this);
        Outline(button);
        lv_obj_set_size(button, 278, 70);
        lv_obj_align(button, LV_ALIGN_BOTTOM_MID, 0, -63);
    }
}

void SpeakerAccountPage::OnUnbind(lv_event_t* event) {
    auto* page = static_cast<SpeakerAccountPage*>(lv_event_get_user_data(event));
    if (page->confirm_unbind_) {
        page->confirm_unbind_ = false;
        Application::GetInstance().Schedule([] { SpeakerAccount::Get().Unbind(); });
    } else {
        page->confirm_unbind_ = true;
    }
    page->Render();
}

void RenderSpeakerAccount(const SpeakerAccount::View& view) {
    auto* display = Board::GetInstance().GetDisplay();
    if (!display->IsSetupUICalled() || !display->SupportsGuiOperations()) {
        return;
    }
    DisplayLockGuard lock(display);
    if (lock && account_page) {
        account_page->Apply(view);
    }
}
