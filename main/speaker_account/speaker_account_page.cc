#include "speaker_account_page.h"
#include "speaker_recharge.h"
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
        case Page::Scan: return "请用手机扫描二维码";
        case Page::Scanned: return "请在手机上完成授权";
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
    SpeakerRecharge::Get().Close();
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
    auto& account = SpeakerAccount::Get();
    account.RefreshBalance();
    view_ = account.GetView();
    Render();
}

void SpeakerAccountPage::Hide() {
    requested_ = false;
    SpeakerRecharge::Get().Close();
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
        if (!view.recharge_available || view.balance_permission != view_.balance_permission) {
            SpeakerRecharge::Get().Close();
        }
        const bool reset_confirmation = view_.page != view.page || view_.customer != view.customer ||
                                        !view.can_unbind || view.unbind_pending;
        if (reset_confirmation) {
            confirm_unbind_ = false;
        }
        if (requested_ && view.balance_permission > 0 && view_.balance_permission != view.balance_permission) {
            auto& account = SpeakerAccount::Get();
            account.RefreshBalance();
            view_ = account.GetView();
        } else {
            view_ = view;
        }
        Render();
    }
}

void SpeakerAccountPage::RefreshRecharge() {
    if (requested_ && !PromptVisible() && SpeakerRecharge::Get().GetView().page != SpeakerRecharge::Page::Inactive) {
        Render();
    }
}

bool SpeakerAccountPage::PromptVisible() const {
    return prompts_allowed_ && (view_.page == Page::Scan || view_.page == Page::Scanned || view_.page == Page::Retry ||
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
    if (!prompt && requested_ && SpeakerRecharge::Get().GetView().page != SpeakerRecharge::Page::Inactive) {
        RenderRecharge();
    } else if (prompt || requested_) {
        RenderContent(prompt);
    }
}

void SpeakerAccountPage::RenderRecharge() {
    using RechargePage = SpeakerRecharge::Page;
    const auto view = SpeakerRecharge::Get().GetView();
    Header(root_, view.page == RechargePage::Consent ? "充值授权" : "积分充值", OnRechargeBack, this);
    auto* status = Label(root_, view.message.c_str(), kCyan);
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    if (!view.url.empty() && RenderQr(root_, view.url, qr_buffer_)) {
        lv_obj_set_width(status, 350);
        lv_obj_align(status, LV_ALIGN_CENTER, 170, -36);
        const auto* hint = view.page == RechargePage::Consent ? view.code.c_str() : "在集市页面选择金额并付款";
        auto* detail = Label(root_, hint, kMuted);
        lv_obj_align(detail, LV_ALIGN_CENTER, 170, 35);
        return;
    }
    if (!view.url.empty()) {
        UpdateLabel(status, "二维码生成失败，请重新获取");
    }
    lv_obj_set_width(status, 640);
    lv_obj_align(status, LV_ALIGN_CENTER, 0, -22);
    if (view.page == RechargePage::Failed || view.page == RechargePage::Waiting || !view.url.empty()) {
        auto* retry = Button(root_, "重新获取", OnRecharge, this, kCyan, kDeep);
        lv_obj_set_size(retry, 212, 64);
        lv_obj_align(retry, LV_ALIGN_CENTER, 0, 78);
    } else if (view.page == RechargePage::Paid) {
        auto* done = Button(root_, "返回账号", OnRechargeBack, this, kCyan, kDeep);
        lv_obj_set_size(done, 212, 64);
        lv_obj_align(done, LV_ALIGN_CENTER, 0, 78);
    }
}

void SpeakerAccountPage::RenderContent(bool prompt) {
    const bool scanned = view_.page == Page::Scanned;
    const bool scan = view_.page == Page::Scan || scanned || view_.page == Page::Retry;
    Header(root_, scan ? "扫码登录" : "账号", prompt ? nullptr : back_, back_context_);
    auto* state = Label(root_, view_.unbind_pending ? "正在解除绑定" : Description(view_.page), kCyan);
    lv_obj_align(state, LV_ALIGN_CENTER, 0, -18);
    if (view_.unbind_pending) {
        return;
    }

    const auto add_refresh = [this](int x, int y) {
        auto* button = Button(root_, "刷新二维码", [](lv_event_t*) {
            SpeakerAccount::Get().RefreshCode();
        }, nullptr, kCyan, kDeep);
        lv_obj_set_size(button, 225, 70);
        lv_obj_align(button, LV_ALIGN_CENTER, x, y);
    };
    if (scanned || (view_.page == Page::Scan && RenderQr(root_, view_.qr_url, qr_buffer_))) {
        lv_obj_set_width(state, 346);
        lv_obj_set_style_text_align(state, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(state, LV_ALIGN_CENTER, 171, -72);
        auto* caption = Label(root_, "核对授权码", kMuted);
        Scale(caption, 220);
        lv_obj_align(caption, LV_ALIGN_CENTER, 171, -4);
        auto* code = Label(root_, view_.user_code.c_str());
        Scale(code, 330);
        lv_obj_align(code, LV_ALIGN_CENTER, 171, 69);
        if (scanned) {
            lv_obj_align(Label(root_, "已扫码", kCyan), LV_ALIGN_CENTER, -196, -41);
            add_refresh(-196, 53);
        }
        return;
    }
    if (scan) {
        if (view_.page == Page::Scan) {
            UpdateLabel(state, "二维码生成失败，请重新获取");
        }
        add_refresh(0, 90);
        return;
    }
    if (view_.page == Page::Unprovisioned) {
        lv_obj_align(Label(root_, SpeakerAccount::Get().hardware_id().c_str(), kMuted),
                     LV_ALIGN_CENTER, 0, 48);
        return;
    }
    if (view_.page != Page::Ready && view_.customer.empty() && view_.balance_permission <= 0) {
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
    if (view_.balance_permission > 0) {
        auto* caption = Label(root_, "积分", kMuted);
        lv_obj_set_pos(caption, 382, 266);
        const char* value = view_.balance_loading ? "正在查询" :
                            view_.balance.empty() ? "暂时无法获取" : view_.balance.c_str();
        auto* balance = Label(root_, value);
        constexpr int kBalanceWidth = 318;
        const auto* font = lv_obj_get_style_text_font(balance, LV_PART_MAIN);
        lv_point_t size;
        lv_text_get_size(&size, value, font, lv_obj_get_style_text_letter_space(balance, LV_PART_MAIN),
                         lv_obj_get_style_text_line_space(balance, LV_PART_MAIN), kBalanceWidth, LV_TEXT_FLAG_NONE);
        if (!view_.balance_loading && !view_.balance.empty() && size.y > lv_font_get_line_height(font)) {
            lv_obj_set_style_text_font(balance, LV_FONT_DEFAULT, LV_PART_MAIN);
        }
        lv_obj_set_width(balance, kBalanceWidth);
        lv_obj_align_to(balance, caption, LV_ALIGN_OUT_RIGHT_MID, 20, 0);
    }
    if (view_.can_unbind && confirm_unbind_) {
        auto* cancel = Button(root_, "取消", [](lv_event_t* event) {
            auto* page = static_cast<SpeakerAccountPage*>(lv_event_get_user_data(event));
            page->confirm_unbind_ = false;
            page->Render();
        }, this);
        Outline(cancel, kEdge);
        lv_obj_set_size(cancel, 250, 70);
        lv_obj_align(cancel, LV_ALIGN_BOTTOM_MID, -150, -63);
        auto* confirm = Button(root_, "确认解绑", OnUnbind, this, kCyan, kDeep);
        lv_obj_set_size(confirm, 278, 70);
        lv_obj_align(confirm, LV_ALIGN_BOTTOM_MID, 150, -63);
        return;
    }
    if (view_.can_unbind) {
        auto* button = Button(root_, "解除绑定", OnUnbind, this);
        Outline(button);
        lv_obj_set_size(button, 278, 70);
        lv_obj_align(button, LV_ALIGN_BOTTOM_MID, view_.recharge_available ? 150 : 0, -63);
    }
    if (view_.recharge_available) {
        auto* button = Button(root_, "充值", OnRecharge, this, kCyan, kDeep);
        lv_obj_set_size(button, 250, 70);
        lv_obj_align(button, LV_ALIGN_BOTTOM_MID, -150, -63);
    }
}

void SpeakerAccountPage::OnRecharge(lv_event_t* event) {
    auto* page = static_cast<SpeakerAccountPage*>(lv_event_get_user_data(event));
    page->confirm_unbind_ = false;
    SpeakerRecharge::Get().Open();
    page->Render();
}

void SpeakerAccountPage::OnRechargeBack(lv_event_t* event) {
    auto* page = static_cast<SpeakerAccountPage*>(lv_event_get_user_data(event));
    auto& recharge = SpeakerRecharge::Get();
    const bool paid = recharge.GetView().page == SpeakerRecharge::Page::Paid;
    recharge.Close();
    if (paid) SpeakerAccount::Get().RefreshBalance();
    page->Render();
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

void RenderSpeakerRecharge() {
    auto* display = Board::GetInstance().GetDisplay();
    if (!display->IsSetupUICalled() || !display->SupportsGuiOperations()) return;
    DisplayLockGuard lock(display);
    if (lock && account_page) account_page->RefreshRecharge();
}
