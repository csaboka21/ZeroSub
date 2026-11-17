/*
 * SPDX-License-Identifier: MIT
 */

#include "read_raw_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "page_keys.h"
#include "theme.h"

namespace screen {
namespace {

using viewmodel::BaseViewModel;

constexpr int32_t kSideMargin = 8;
constexpr int32_t kChartY = 19;
constexpr int32_t kChartWidth = view::kScreenWidth - 2 * kSideMargin;
constexpr int32_t kChartHeight = 42;
/* One point per RSSI reading: about three seconds of history. */
constexpr uint32_t kChartPoints = 100;

} // namespace

ReadRawScreen::ReadRawScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

std::vector<view::widgets::NavHint> ReadRawScreen::nav_hints() const {
    return {{"ESC", "Back"}, {"ENTER", "Rec / Stop"}, {"Fn+H", "Help"}};
}

model::RawState ReadRawScreen::state() {
    return static_cast<model::RawState>(lv_subject_get_int(view_model().raw_state_subject()));
}

bool ReadRawScreen::on_key(uint32_t key) {
    switch (state()) {
        case model::RawState::Idle:
            if (is_left_key(key)) {
                view_model().previous_frequency();
                return true;
            }
            if (is_right_key(key)) {
                view_model().next_frequency();
                return true;
            }
            if (is_up_key(key)) {
                view_model().previous_preset();
                return true;
            }
            if (is_down_key(key) || key == '5') {
                view_model().next_preset();
                return true;
            }
            if (key == '6') {
                view_model().raw_next_threshold();
                return true;
            }
            if (is_ok_key(key)) {
                view_model().raw_toggle_record();
                return true;
            }
            break;
        case model::RawState::Recording:
            if (is_ok_key(key)) {
                view_model().raw_toggle_record();
                return true;
            }
            break;
        case model::RawState::Stopped:
            if (is_right_key(key)) {
                view_model().raw_show_info();
                return true;
            }
            /* Erase is on a digit: a navigation key should never discard a recording. */
            if (key == '8') {
                view_model().raw_erase();
                return true;
            }
            if (is_ok_key(key)) {
                view_model().raw_save();
                return true;
            }
            break;
    }
    return false;
}

void ReadRawScreen::build_content(lv_obj_t* content) {
    const lv_font_t* bold_font = assets().load_standard_font(12);
    if (!bold_font) {
        bold_font = &lv_font_montserrat_12;
    }
    const lv_font_t* small_font = assets().load_standard_font(11, app::StandardFontWeight::Regular);
    if (!small_font) {
        small_font = &lv_font_montserrat_12;
    }

    auto* frequency = lv_label_create(content);
    lv_label_bind_text(frequency, view_model().radio_frequency_subject(), nullptr);
    lv_obj_set_style_text_font(frequency, bold_font, 0);
    lv_obj_align(frequency, LV_ALIGN_TOP_LEFT, kSideMargin, 2);
    reactive::bind_theme(frequency, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* preset = lv_label_create(content);
    lv_label_bind_text(preset, view_model().radio_preset_subject(), nullptr);
    lv_obj_set_style_text_font(preset, small_font, 0);
    lv_obj_align(preset, LV_ALIGN_TOP_MID, 0, 3);
    reactive::bind_theme(preset, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    threshold_label_ = lv_label_create(content);
    lv_label_bind_text(threshold_label_, view_model().raw_threshold_subject(), "Thr %s");
    lv_obj_set_style_text_font(threshold_label_, small_font, 0);
    lv_obj_align(threshold_label_, LV_ALIGN_TOP_RIGHT, -kSideMargin, 3);

    chart_ = lv_chart_create(content);
    lv_obj_remove_style_all(chart_);
    lv_obj_set_size(chart_, kChartWidth, kChartHeight);
    lv_obj_align(chart_, LV_ALIGN_TOP_LEFT, kSideMargin, kChartY);
    lv_obj_set_style_border_width(chart_, 1, 0);
    lv_obj_set_style_radius(chart_, 2, 0);
    lv_obj_set_style_line_width(chart_, 1, LV_PART_ITEMS);
    /* No point markers: only the line. */
    lv_obj_set_style_size(chart_, 0, 0, LV_PART_INDICATOR);
    lv_chart_set_type(chart_, LV_CHART_TYPE_LINE);
    lv_chart_set_div_line_count(chart_, 0, 0);
    lv_chart_set_point_count(chart_, kChartPoints);
    lv_chart_set_update_mode(chart_, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_axis_range(chart_,
                            LV_CHART_AXIS_PRIMARY_Y,
                            BaseViewModel::kRssiFloorDbm,
                            BaseViewModel::kRssiCeilingDbm);
    series_ = lv_chart_add_series(chart_,
                                  view::palette(view_model().is_dark_mode()).primary,
                                  LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_values(chart_, series_, BaseViewModel::kRssiFloorDbm);

    threshold_line_ = lv_obj_create(content);
    lv_obj_remove_style_all(threshold_line_);
    lv_obj_set_size(threshold_line_, kChartWidth - 2, 1);
    lv_obj_set_style_bg_opa(threshold_line_, LV_OPA_COVER, 0);

    state_label_ = lv_label_create(content);
    lv_obj_set_style_text_font(state_label_, bold_font, 0);
    lv_obj_align(state_label_, LV_ALIGN_TOP_LEFT, kSideMargin, 64);

    keys_label_ = lv_label_create(content);
    lv_obj_set_style_text_font(keys_label_, small_font, 0);
    lv_obj_align(keys_label_, LV_ALIGN_TOP_LEFT, kSideMargin, 79);
    reactive::bind_theme(keys_label_, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    info_label_ = lv_label_create(content);
    lv_label_set_long_mode(info_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(info_label_, kChartWidth);
    lv_obj_set_style_text_font(info_label_, small_font, 0);
    lv_obj_align(info_label_, LV_ALIGN_TOP_LEFT, kSideMargin, 94);

    /* All observers are bound to the chart object and fire once on registration. */
    lv_subject_add_observer_obj(view_model().raw_tick_subject(), tick_observer_cb, chart_, this);
    for (auto* subject : {view_model().raw_state_subject(),
                          view_model().raw_sample_count_subject(),
                          view_model().raw_threshold_subject(),
                          view_model().raw_message_subject(),
                          view_model().radio_status_subject(),
                          view_model().dark_mode_subject()}) {
        lv_subject_add_observer_obj(subject, refresh_observer_cb, chart_, this);
    }
}

void ReadRawScreen::plot_reading() {
    if (chart_ && series_) {
        lv_chart_set_next_value(chart_, series_, lv_subject_get_int(view_model().radio_rssi_subject()));
    }
}

void ReadRawScreen::refresh() {
    if (!info_label_) {
        /* Called while the screen is still being built. */
        return;
    }

    const auto colors = view::palette(view_model().is_dark_mode());
    const int samples = lv_subject_get_int(view_model().raw_sample_count_subject());

    lv_obj_set_style_border_color(chart_, colors.border, 0);
    lv_chart_set_series_color(chart_, series_, colors.primary);
    lv_obj_set_style_text_color(threshold_label_, colors.warning, 0);

    int32_t threshold_dbm = 0;
    if (view_model().raw_threshold_dbm(threshold_dbm)) {
        const int32_t span = BaseViewModel::kRssiCeilingDbm - BaseViewModel::kRssiFloorDbm;
        const int32_t from_top =
            (BaseViewModel::kRssiCeilingDbm - threshold_dbm) * (kChartHeight - 1) / span;
        lv_obj_set_style_bg_color(threshold_line_, colors.warning, 0);
        lv_obj_align(threshold_line_, LV_ALIGN_TOP_LEFT, kSideMargin + 1, kChartY + from_top);
        lv_obj_remove_flag(threshold_line_, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(threshold_line_, LV_OBJ_FLAG_HIDDEN);
    }

    switch (state()) {
        case model::RawState::Idle:
            lv_label_set_text(state_label_, "READY");
            lv_obj_set_style_text_color(state_label_, colors.text, 0);
            lv_label_set_text(keys_label_, "ENTER Rec   Z/C Freq   F/X Mod   6 Threshold");
            break;
        case model::RawState::Recording:
            lv_label_set_text_fmt(state_label_, "REC   %d pulses", samples);
            lv_obj_set_style_text_color(state_label_, colors.error, 0);
            lv_label_set_text(keys_label_, "ENTER Stop");
            break;
        case model::RawState::Stopped:
            lv_label_set_text_fmt(state_label_, "STOPPED   %d pulses", samples);
            lv_obj_set_style_text_color(state_label_, colors.text, 0);
            lv_label_set_text(keys_label_, "ENTER Save   C Info   8 Erase");
            break;
    }

    /* A capture message takes priority over the radio status line. */
    const char* message = lv_subject_get_string(view_model().raw_message_subject());
    const bool has_message = message && message[0] != '\0';
    lv_label_set_text(info_label_,
                      has_message ? message : lv_subject_get_string(view_model().radio_status_subject()));
    lv_obj_set_style_text_color(info_label_, has_message ? colors.primary : colors.text_disabled, 0);
}

void ReadRawScreen::refresh_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<ReadRawScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

void ReadRawScreen::tick_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<ReadRawScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->plot_reading();
    }
}

} // namespace screen
