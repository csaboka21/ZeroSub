/*
 * SPDX-License-Identifier: MIT
 */

#include "frequency_analyzer_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "theme.h"

namespace screen {
namespace {

using viewmodel::BaseViewModel;

constexpr int32_t kChartHeight = 36;
constexpr int32_t kBarWidth = 12;
constexpr int32_t kBarGap = 6;
constexpr int32_t kMinBarHeight = 2;
constexpr int32_t kChartWidth =
    static_cast<int32_t>(BaseViewModel::kAnalyzerChannelCount) * (kBarWidth + kBarGap) - kBarGap;

int32_t bar_height(int32_t rssi_dbm) {
    const int32_t span = BaseViewModel::kRssiCeilingDbm - BaseViewModel::kRssiFloorDbm;
    const int32_t height = (rssi_dbm - BaseViewModel::kRssiFloorDbm) * kChartHeight / span;
    return height < kMinBarHeight ? kMinBarHeight : (height > kChartHeight ? kChartHeight : height);
}

} // namespace

FrequencyAnalyzerScreen::FrequencyAnalyzerScreen(viewmodel::BaseViewModel& view_model,
                                                 app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

std::vector<view::widgets::NavHint> FrequencyAnalyzerScreen::nav_hints() const {
    return {{"ESC", "Back"}, {"Range", "300-925 MHz"}};
}

void FrequencyAnalyzerScreen::build_content(lv_obj_t* content) {
    const lv_font_t* large_font = assets().load_standard_font(20);
    if (!large_font) {
        large_font = &lv_font_montserrat_20;
    }
    const lv_font_t* small_font = assets().load_standard_font(11, app::StandardFontWeight::Regular);
    if (!small_font) {
        small_font = &lv_font_montserrat_12;
    }

    peak_label_ = lv_label_create(content);
    lv_label_bind_text(peak_label_, view_model().analyzer_peak_frequency_subject(), nullptr);
    lv_obj_set_style_text_font(peak_label_, large_font, 0);
    lv_obj_align(peak_label_, LV_ALIGN_TOP_MID, 0, 2);

    auto* last = lv_label_create(content);
    lv_label_bind_text(last, view_model().analyzer_last_subject(), nullptr);
    lv_obj_set_style_text_font(last, small_font, 0);
    lv_obj_align(last, LV_ALIGN_TOP_MID, 0, 28);
    reactive::bind_theme(last, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* chart = lv_obj_create(content);
    lv_obj_remove_style_all(chart);
    lv_obj_set_size(chart, kChartWidth, kChartHeight);
    lv_obj_align(chart, LV_ALIGN_TOP_MID, 0, 46);
    lv_obj_clear_flag(chart, LV_OBJ_FLAG_SCROLLABLE);

    for (std::size_t index = 0; index < bars_.size(); ++index) {
        auto* bar = lv_obj_create(chart);
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, kBarWidth, kMinBarHeight);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(bar, 1, 0);
        lv_obj_align(bar,
                     LV_ALIGN_BOTTOM_LEFT,
                     static_cast<int32_t>(index) * (kBarWidth + kBarGap),
                     0);
        bars_[index] = bar;
    }

    auto* status = lv_label_create(content);
    lv_label_bind_text(status, view_model().radio_status_subject(), nullptr);
    lv_label_set_long_mode(status, LV_LABEL_LONG_DOT);
    lv_obj_set_width(status, 300);
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(status, small_font, 0);
    lv_obj_align(status, LV_ALIGN_BOTTOM_MID, 0, -3);
    reactive::bind_theme(status, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    /* Both observers are bound to the chart object and fire once on registration. */
    lv_subject_add_observer_obj(view_model().analyzer_revision_subject(), refresh_observer_cb, chart, this);
    lv_subject_add_observer_obj(view_model().dark_mode_subject(), refresh_observer_cb, chart, this);
}

void FrequencyAnalyzerScreen::refresh() {
    const auto colors = view::palette(view_model().is_dark_mode());
    const std::size_t peak = view_model().analyzer_peak_channel();
    const bool detected = peak < bars_.size();

    if (peak_label_) {
        lv_obj_set_style_text_color(peak_label_, detected ? colors.primary : colors.text_disabled, 0);
    }
    for (std::size_t index = 0; index < bars_.size(); ++index) {
        if (!bars_[index]) {
            continue;
        }
        lv_obj_set_height(bars_[index], bar_height(view_model().analyzer_channel_rssi(index)));
        lv_obj_set_style_bg_color(bars_[index], index == peak ? colors.primary : colors.border, 0);
    }
}

void FrequencyAnalyzerScreen::refresh_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<FrequencyAnalyzerScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen
