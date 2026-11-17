/*
 * SPDX-License-Identifier: MIT
 */

#include "signal_meter_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "page_keys.h"

namespace screen {
namespace {

void rssi_bar_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* bar = lv_observer_get_target_obj(observer);
    if (bar) {
        lv_bar_set_value(bar, lv_subject_get_int(subject), LV_ANIM_OFF);
    }
}

/* The reading is hidden while the radio is not receiving, so no stale value is shown. */
void radio_ready_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* object = lv_observer_get_target_obj(observer);
    if (!object) {
        return;
    }

    if (lv_subject_get_int(subject)) {
        lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    }
}

} // namespace

SignalMeterScreen::SignalMeterScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

std::vector<view::widgets::NavHint> SignalMeterScreen::nav_hints() const {
    return {{"ESC", "Back"}, {"Z/C", "Freq"}, {"F/X", "Modulation"}};
}

bool SignalMeterScreen::on_key(uint32_t key) {
    /* Z / C tune, F / X change the modulation. */
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
    if (is_down_key(key)) {
        view_model().next_preset();
        return true;
    }
    return false;
}

void SignalMeterScreen::build_content(lv_obj_t* content) {
    auto* group = lv_obj_create(content);
    lv_obj_remove_style_all(group);
    lv_obj_set_size(group, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(group, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(group, 4, 0);
    lv_obj_clear_flag(group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(group);

    const lv_font_t* large_font = assets().load_standard_font(20);
    if (!large_font) {
        large_font = &lv_font_montserrat_20;
    }
    const lv_font_t* small_font = assets().load_standard_font(12, app::StandardFontWeight::Regular);
    if (!small_font) {
        small_font = &lv_font_montserrat_12;
    }

    auto* frequency = lv_label_create(group);
    lv_label_bind_text(frequency, view_model().radio_frequency_subject(), nullptr);
    lv_obj_set_style_text_font(frequency, large_font, 0);
    reactive::bind_theme(frequency, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* preset = lv_label_create(group);
    lv_label_bind_text(preset, view_model().radio_preset_subject(), "Modulation %s");
    lv_obj_set_style_text_font(preset, small_font, 0);
    reactive::bind_theme(preset, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* rssi = lv_label_create(group);
    lv_label_bind_text(rssi, view_model().radio_rssi_subject(), "RSSI %d dBm");
    lv_obj_set_style_text_font(rssi, small_font, 0);
    reactive::bind_theme(rssi, view_model().dark_mode_subject(), reactive::ThemeRole::Text);
    lv_subject_add_observer_obj(view_model().radio_ready_subject(), radio_ready_observer, rssi, nullptr);

    auto* bar = lv_bar_create(group);
    lv_obj_set_size(bar, 220, 8);
    lv_bar_set_range(bar,
                     viewmodel::BaseViewModel::kRssiFloorDbm,
                     viewmodel::BaseViewModel::kRssiCeilingDbm);
    lv_subject_add_observer_obj(view_model().radio_rssi_subject(), rssi_bar_observer, bar, nullptr);
    lv_subject_add_observer_obj(view_model().radio_ready_subject(), radio_ready_observer, bar, nullptr);

    auto* status = lv_label_create(group);
    lv_label_bind_text(status, view_model().radio_status_subject(), nullptr);
    lv_label_set_long_mode(status, LV_LABEL_LONG_DOT);
    lv_obj_set_width(status, 300);
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(status, small_font, 0);
    reactive::bind_theme(status, view_model().dark_mode_subject(), reactive::ThemeRole::Text);
}

} // namespace screen
