#include "speaker_account.h"

#include "application.h"
#include "board.h"
#include "display.h"
#include "lvgl_font.h"

#include <lvgl.h>
#include <qrcode_wrapper.h>

namespace {

using Page = SpeakerAccount::Page;

lv_obj_t* launcher = nullptr;
lv_obj_t* panel = nullptr;
lv_draw_buf_t* qr_buffer = nullptr;
bool details_open = false;
bool confirm_unbind = false;
SpeakerAccount::View rendered;
bool has_rendered = false;

void RefreshView() {
    Application::GetInstance().Schedule([] {
        has_rendered = false;
        RenderSpeakerAccount(SpeakerAccount::Get().GetView());
    });
}

void ToggleDetails(lv_event_t*) {
    details_open = !details_open;
    confirm_unbind = false;
    RefreshView();
}

void Retry(lv_event_t*) {
    SpeakerAccount::Get().RefreshCode();
}

void ToggleUnbind(lv_event_t*) {
    if (confirm_unbind) {
        SpeakerAccount::Get().Unbind();
        confirm_unbind = false;
    } else {
        confirm_unbind = true;
    }
    RefreshView();
}

lv_obj_t* Label(lv_obj_t* parent, const char* text, lv_color_t color) {
    auto* label = lv_label_create(parent);
    lv_obj_set_width(label, lv_pct(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, text);
    return label;
}

lv_obj_t* Button(lv_obj_t* parent, const char* content, lv_event_cb_t handler) {
    auto* button = lv_button_create(parent);
    lv_obj_set_size(button, 116, 38);
    lv_obj_set_style_radius(button, 12, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x175CD3), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    auto* label = lv_label_create(button);
    lv_label_set_text(label, content);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, handler, LV_EVENT_CLICKED, nullptr);
    return button;
}

const char* Description(Page page) {
    switch (page) {
        case Page::Connecting:
            return "正在连接账号服务";
        case Page::Unprovisioned:
            return "设备身份尚未交付";
        case Page::Scan:
            return "用手机扫描二维码并核对授权码";
        case Page::Retry:
            return "授权未完成，请重新获取二维码";
        case Page::Disabled:
            return "当前无法使用，请联系管理员";
        case Page::Ready:
            return "已绑定 AI 集市账号";
        case Page::Offline:
            return "连接中断，正在恢复";
    }
    return "";
}

bool RenderQr(lv_obj_t* parent, const std::string& url) {
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
        .user_data = &qr_buffer,
    };
    if (qrcode_wrapper_generate(&config, url.c_str()) != ESP_OK || !qr_buffer) {
        return false;
    }
    auto* canvas = lv_canvas_create(parent);
    lv_canvas_set_draw_buf(canvas, qr_buffer);
    return true;
}

}  // namespace

void RenderSpeakerAccount(const SpeakerAccount::View& view) {
    auto* display = Board::GetInstance().GetDisplay();
    if (!display->IsSetupUICalled() || !display->SupportsGuiOperations()) {
        return;
    }
    DisplayLockGuard lock(display);
    if (has_rendered && rendered == view) {
        return;
    }
    has_rendered = true;
    rendered = view;
    if (!view.can_unbind || view.unbind_pending) {
        confirm_unbind = false;
    }
    auto* layer = lv_layer_top();
    if (!launcher) {
        launcher = Button(layer, "AI 集市", ToggleDetails);
        lv_obj_align(launcher, LV_ALIGN_TOP_RIGHT, -10, 45);
    }
    if (panel) {
        lv_obj_delete(panel);
        panel = nullptr;
    }
    if (qr_buffer) {
        lv_draw_buf_destroy(qr_buffer);
        qr_buffer = nullptr;
    }
    const bool show = details_open || view.page == Page::Scan || view.page == Page::Retry ||
                      view.page == Page::Unprovisioned || view.unbind_pending;
    if (!show || Application::GetInstance().GetDeviceState() == kDeviceStateUpgrading) {
        return;
    }

    panel = lv_obj_create(layer);
    lv_obj_set_size(panel, 392, LV_SIZE_CONTENT);
    lv_obj_center(panel);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(panel, 18, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0xD2DAE5), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_pad_all(panel, 22, 0);
    lv_obj_set_style_pad_row(panel, 12, 0);
    if (auto* theme = display->GetTheme()) {
        if (const auto font = theme->GetTextFont()) {
            lv_obj_set_style_text_font(panel, font->font(), 0);
            lv_obj_set_style_text_font(launcher, font->font(), 0);
        }
    }
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    Label(panel, "AI 集市", lv_color_hex(0x172334));
    Label(panel, view.unbind_pending ? "正在解除绑定" : Description(view.page), lv_color_hex(0x34465B));
    if (view.page == Page::Unprovisioned) {
        Label(panel, SpeakerAccount::Get().hardware_id().c_str(), lv_color_hex(0x172334));
    }

    if (view.page == Page::Scan && !view.unbind_pending) {
        if (!RenderQr(panel, view.qr_url)) {
            Label(panel, "二维码生成失败，请重新获取", lv_color_hex(0x9B2525));
            Button(panel, "重新获取", Retry);
        }
        Label(panel, view.user_code.c_str(), lv_color_hex(0x172334));
    } else if (view.page == Page::Retry && !view.unbind_pending) {
        Button(panel, "重新获取", Retry);
    } else if (!view.customer.empty()) {
        Label(panel, view.customer.c_str(), lv_color_hex(0x172334));
    }
    if (details_open && view.can_unbind && !view.unbind_pending) {
        Button(panel, confirm_unbind ? "确认解绑" : "解除绑定", ToggleUnbind);
    }
}
