/*
 * SPDX-License-Identifier: MIT
 */

#include "saved_screen.h"

#include "asset_manager.h"
#include "page_keys.h"

namespace screen {
namespace {

constexpr int32_t kSideMargin = 8;
constexpr int32_t kListY = 4;
constexpr int32_t kTextWidth = view::kScreenWidth - 2 * kSideMargin;

} // namespace

SavedScreen::SavedScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

std::vector<view::widgets::NavHint> SavedScreen::nav_hints() const {
    return {{"ESC", "Back"},
            {"F/X", "Move"},
            {"Z", "Up"},
            {"C", "Open"},
            {"ENTER", "Send"},
            {"8", "Delete"}};
}

bool SavedScreen::on_key(uint32_t key) {
    if (is_right_key(key)) {
        view_model().saved_show_info();
        return true;
    }
    if (is_left_key(key)) {
        view_model().saved_up();
        return true;
    }
    if (is_up_key(key)) {
        view_model().saved_previous();
        return true;
    }
    if (is_down_key(key)) {
        view_model().saved_next();
        return true;
    }
    if (is_ok_key(key)) {
        view_model().saved_send();
        return true;
    }
    if (key == '8') {
        view_model().saved_delete();
        return true;
    }
    return false;
}

void SavedScreen::build_content(lv_obj_t* content) {
    const lv_font_t* bold_font = assets().load_standard_font(12);
    if (!bold_font) {
        bold_font = &lv_font_montserrat_12;
    }
    const lv_font_t* small_font = assets().load_standard_font(11, app::StandardFontWeight::Regular);
    if (!small_font) {
        small_font = &lv_font_montserrat_12;
    }

    rows_.build(content, kListY, small_font);

    empty_label_ = lv_label_create(content);
    lv_label_set_text(empty_label_, "This folder is empty.");
    lv_obj_set_style_text_font(empty_label_, small_font, 0);
    lv_obj_align(empty_label_, LV_ALIGN_TOP_MID, 0, kListY + 26);

    summary_label_ = lv_label_create(content);
    lv_label_set_long_mode(summary_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(summary_label_, kTextWidth);
    lv_obj_set_style_text_font(summary_label_, bold_font, 0);
    lv_obj_align(summary_label_, LV_ALIGN_TOP_LEFT, kSideMargin, 76);

    info_label_ = lv_label_create(content);
    lv_label_set_long_mode(info_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(info_label_, kTextWidth);
    lv_obj_set_style_text_font(info_label_, small_font, 0);
    lv_obj_align(info_label_, LV_ALIGN_TOP_LEFT, kSideMargin, 94);

    /* All observers are bound to the info label and fire once on registration. */
    for (auto* subject : {view_model().saved_revision_subject(),
                          view_model().saved_selection_subject(),
                          view_model().saved_message_subject(),
                          view_model().dark_mode_subject()}) {
        lv_subject_add_observer_obj(subject, refresh_observer_cb, info_label_, this);
    }
}

void SavedScreen::refresh() {
    if (!info_label_) {
        return;
    }

    const auto colors = view::palette(view_model().is_dark_mode());
    const std::size_t count = view_model().saved_count();
    const auto selected =
        static_cast<std::size_t>(lv_subject_get_int(view_model().saved_selection_subject()));

    rows_.render(count,
                 selected,
                 [this](std::size_t index) {
                     const auto* file = view_model().saved_entry(index);
                     if (!file) {
                         return std::string();
                     }
                     return file->is_directory ? file->name + "/" : file->name;
                 },
                 colors);

    lv_obj_set_style_text_color(empty_label_, colors.text_disabled, 0);
    if (count == 0) {
        lv_obj_remove_flag(empty_label_, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(empty_label_, LV_OBJ_FLAG_HIDDEN);
    }

    const auto* file = view_model().saved_entry(selected);
    lv_label_set_text(summary_label_, file ? file->summary.c_str() : "");
    lv_obj_set_style_text_color(summary_label_, colors.primary, 0);

    lv_label_set_text(info_label_, lv_subject_get_string(view_model().saved_message_subject()));
    lv_obj_set_style_text_color(info_label_, colors.text_disabled, 0);
}

void SavedScreen::refresh_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<SavedScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen
