/*
 * SPDX-License-Identifier: MIT
 */

#include "read_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "page_keys.h"

namespace screen {
namespace {

constexpr int32_t kSideMargin = 8;
constexpr int32_t kListY = 20;

} // namespace

ReadScreen::ReadScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

std::vector<view::widgets::NavHint> ReadScreen::nav_hints() const {
    return {{"ESC", "Back"}, {"F/X", "Move"}, {"C", "Info"}, {"ENTER", "Save"}};
}

bool ReadScreen::on_key(uint32_t key) {
    /* F / X move, C opens the summary; left is unused. */
    if (is_right_key(key)) {
        view_model().read_show_info();
        return true;
    }
    if (is_up_key(key)) {
        view_model().read_previous();
        return true;
    }
    if (is_down_key(key)) {
        view_model().read_next();
        return true;
    }
    if (is_ok_key(key)) {
        view_model().read_save_selected();
        return true;
    }
    switch (key) {
        case '5':
            view_model().next_preset();
            return true;
        case '7':
            view_model().previous_frequency();
            return true;
        case '8':
            view_model().next_frequency();
            return true;
        default:
            return false;
    }
}

void ReadScreen::build_content(lv_obj_t* content) {
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

    auto* rssi = lv_label_create(content);
    lv_label_bind_text(rssi, view_model().radio_rssi_subject(), "%d dBm");
    lv_obj_set_style_text_font(rssi, small_font, 0);
    lv_obj_align(rssi, LV_ALIGN_TOP_RIGHT, -kSideMargin, 3);
    reactive::bind_theme(rssi, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    rows_.build(content, kListY, small_font);

    empty_label_ = lv_label_create(content);
    lv_label_set_text(empty_label_, "Waiting for a signal.\nHold a remote's button near the antenna.");
    lv_obj_set_style_text_align(empty_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(empty_label_, small_font, 0);
    lv_obj_align(empty_label_, LV_ALIGN_TOP_MID, 0, kListY + 18);

    info_label_ = lv_label_create(content);
    lv_label_set_long_mode(info_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(info_label_, view::kScreenWidth - 2 * kSideMargin);
    lv_obj_set_style_text_font(info_label_, small_font, 0);
    lv_obj_align(info_label_, LV_ALIGN_TOP_LEFT, kSideMargin, 94);

    /* All observers are bound to the info label and fire once on registration. */
    for (auto* subject : {view_model().read_revision_subject(),
                          view_model().read_selection_subject(),
                          view_model().read_message_subject(),
                          view_model().radio_status_subject(),
                          view_model().dark_mode_subject()}) {
        lv_subject_add_observer_obj(subject, refresh_observer_cb, info_label_, this);
    }
}

void ReadScreen::refresh() {
    if (!info_label_) {
        return;
    }

    const auto colors = view::palette(view_model().is_dark_mode());
    const std::size_t count = view_model().read_count();
    const auto selected =
        static_cast<std::size_t>(lv_subject_get_int(view_model().read_selection_subject()));

    rows_.render(count,
                 selected,
                 [this](std::size_t index) { return view_model().read_entry_text(index); },
                 colors);

    lv_obj_set_style_text_color(empty_label_, colors.text_disabled, 0);
    if (count == 0) {
        lv_obj_remove_flag(empty_label_, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(empty_label_, LV_OBJ_FLAG_HIDDEN);
    }

    /* A save message takes priority over the radio status line. */
    const char* message = lv_subject_get_string(view_model().read_message_subject());
    const bool has_message = message && message[0] != '\0';
    lv_label_set_text(info_label_,
                      has_message ? message : lv_subject_get_string(view_model().radio_status_subject()));
    lv_obj_set_style_text_color(info_label_, has_message ? colors.primary : colors.text_disabled, 0);
}

void ReadScreen::refresh_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<ReadScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen
