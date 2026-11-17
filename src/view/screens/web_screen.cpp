/*
 * SPDX-License-Identifier: MIT
 */

#include "web_screen.h"

#include "asset_manager.h"
#include "page_keys.h"
#include "theme.h"

#include <string>

namespace screen {
namespace {

constexpr int32_t kSideMargin = 8;
constexpr int32_t kTextWidth = view::kScreenWidth - 2 * kSideMargin;
constexpr int32_t kLabelWidth = 64;
constexpr int32_t kPinY = 36;
constexpr int32_t kFirstRowY = 64;
constexpr int32_t kRowPitch = 14;

const lv_font_t* font_or_default(const lv_font_t* font) {
    return font ? font : &lv_font_montserrat_12;
}

} // namespace

WebScreen::WebScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

std::vector<view::widgets::NavHint> WebScreen::nav_hints() const {
    return {{"ESC", "Stop & back"}, {"ENTER", "New PIN"}};
}

bool WebScreen::on_key(uint32_t key) {
    if (is_ok_key(key)) {
        view_model().web_new_pin();
        return true;
    }
    return false;
}

void WebScreen::build_content(lv_obj_t* content) {
    const lv_font_t* url_font = font_or_default(assets().load_standard_font(14));
    const lv_font_t* bold_font = font_or_default(assets().load_standard_font(12));
    const lv_font_t* small_font =
        font_or_default(assets().load_standard_font(11, app::StandardFontWeight::Regular));
    const lv_font_t* pin_font = font_or_default(assets().load_standard_font(20));

    url_label_ = lv_label_create(content);
    lv_label_set_long_mode(url_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(url_label_, kTextWidth);
    lv_obj_set_style_text_font(url_label_, url_font, 0);
    lv_obj_align(url_label_, LV_ALIGN_TOP_LEFT, kSideMargin, 2);

    hint_label_ = lv_label_create(content);
    lv_label_set_long_mode(hint_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(hint_label_, kTextWidth);
    lv_obj_set_style_text_font(hint_label_, small_font, 0);
    lv_obj_align(hint_label_, LV_ALIGN_TOP_LEFT, kSideMargin, 20);

    pin_caption_ = lv_label_create(content);
    lv_label_set_text(pin_caption_, "PIN");
    lv_obj_set_style_text_font(pin_caption_, small_font, 0);
    lv_obj_align(pin_caption_, LV_ALIGN_TOP_LEFT, kSideMargin, kPinY + 6);

    pin_label_ = lv_label_create(content);
    lv_obj_set_style_text_font(pin_label_, pin_font, 0);
    lv_obj_set_style_text_letter_space(pin_label_, 3, 0);
    lv_obj_align(pin_label_, LV_ALIGN_TOP_LEFT, kSideMargin + kLabelWidth, kPinY);

    static constexpr const char* kLabels[] = {"Folder", "Browsers", "Last"};
    for (std::size_t index = 0; index < rows_.size(); ++index) {
        const int32_t y = kFirstRowY + static_cast<int32_t>(index) * kRowPitch;
        auto& row = rows_[index];

        row.label = lv_label_create(content);
        lv_label_set_text(row.label, kLabels[index]);
        lv_obj_set_style_text_font(row.label, small_font, 0);
        lv_obj_align(row.label, LV_ALIGN_TOP_LEFT, kSideMargin, y);

        row.value = lv_label_create(content);
        lv_label_set_long_mode(row.value, LV_LABEL_LONG_DOT);
        lv_obj_set_width(row.value, kTextWidth - kLabelWidth);
        lv_obj_set_style_text_font(row.value, bold_font, 0);
        lv_obj_align(row.value, LV_ALIGN_TOP_LEFT, kSideMargin + kLabelWidth, y);
    }

    /* Both observers fire once on registration. */
    lv_subject_add_observer_obj(view_model().web_revision_subject(), refresh_observer_cb, url_label_, this);
    lv_subject_add_observer_obj(view_model().dark_mode_subject(), refresh_observer_cb, url_label_, this);
}

void WebScreen::refresh() {
    if (!url_label_) {
        return;
    }
    const auto colors = view::palette(view_model().is_dark_mode());
    const auto& status = view_model().web_status();
    const auto& urls = view_model().web_urls();

    std::string url;
    std::string hint;
    if (!status.running) {
        url = "Server not running";
        hint = status.error.empty() ? "Starting..." : status.error;
    }
    else if (urls.empty()) {
        url = "No network";
        hint = "Connect to Wi-Fi; port " + std::to_string(status.port);
    }
    else {
        url = urls.front();
        hint = urls.size() > 1 ? "or " + urls[1] : "Open this on a phone or PC on the same network";
    }

    lv_label_set_text(url_label_, url.c_str());
    lv_obj_set_style_text_color(url_label_, status.running && !urls.empty() ? colors.primary : colors.warning, 0);
    lv_label_set_text(hint_label_, hint.c_str());
    lv_obj_set_style_text_color(hint_label_, colors.text_disabled, 0);

    lv_label_set_text(pin_label_, status.running ? status.pin.c_str() : "------");
    lv_obj_set_style_text_color(pin_label_, colors.text, 0);
    lv_obj_set_style_text_color(pin_caption_, colors.text_disabled, 0);

    const std::string values[] = {
        status.root.empty() ? "-" : status.root,
        std::to_string(status.sessions) + " logged in",
        status.last_event.empty() ? "-" : status.last_event,
    };
    for (std::size_t index = 0; index < rows_.size(); ++index) {
        lv_label_set_text(rows_[index].value, values[index].c_str());
        lv_obj_set_style_text_color(rows_[index].label, colors.text_disabled, 0);
        lv_obj_set_style_text_color(rows_[index].value, colors.text, 0);
    }
}

void WebScreen::refresh_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<WebScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen
