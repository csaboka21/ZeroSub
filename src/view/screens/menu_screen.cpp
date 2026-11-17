/*
 * SPDX-License-Identifier: MIT
 */

#include "menu_screen.h"

#include "asset_manager.h"
#include "page_keys.h"
#include "theme.h"

#include <array>

namespace screen {
namespace {

constexpr int32_t kRowWidth = 304;
constexpr int32_t kRowHeight = 17;

/* Row titles, in the order of the model's menu item enums. */
constexpr std::array<const char*, model::kMainMenuItemCount> kMainTitles = {
    "Sub-GHz",
    "Infrared",
    "NFC",
    "IMU",
    "Dark mode",
};

constexpr std::array<const char*, model::kSubGhzMenuItemCount> kSubGhzTitles = {
    "Frequency Analyzer",
    "Signal Meter",
    "Read",
    "Read RAW",
    "Saved",
    "Add Manually",
};

constexpr std::array<const char*, model::kInfraredMenuItemCount> kInfraredTitles = {
    "Learn New Remote",
    "Saved Remotes",
};

constexpr std::array<const char*, model::kNfcMenuItemCount> kNfcTitles = {
    "Read",
    "Saved",
};

constexpr std::array<const char*, model::kRfidMenuItemCount> kRfidTitles = {
    "Read",
    "Saved",
    "Add Manually",
};

constexpr std::array<const char*, model::kImuMenuItemCount> kImuTitles = {
    "Motion",
    "Compass",
    "Level",
};

/* The row titles of one menu page. */
const char* const* titles_of(model::AppPage page, std::size_t& count) {
    switch (page) {
        case model::AppPage::SubGhzMenu:
            count = kSubGhzTitles.size();
            return kSubGhzTitles.data();
        case model::AppPage::InfraredMenu:
            count = kInfraredTitles.size();
            return kInfraredTitles.data();
        case model::AppPage::NfcMenu:
            count = kNfcTitles.size();
            return kNfcTitles.data();
        case model::AppPage::RfidMenu:
            count = kRfidTitles.size();
            return kRfidTitles.data();
        case model::AppPage::ImuMenu:
            count = kImuTitles.size();
            return kImuTitles.data();
        default:
            count = kMainTitles.size();
            return kMainTitles.data();
    }
}

} // namespace

MenuScreen::MenuScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, model::AppPage page)
    : BaseScreen(view_model, assets), page_(page) {
    init();
}

std::vector<view::widgets::NavHint> MenuScreen::nav_hints() const {
    if (page_ == model::AppPage::Menu) {
        return {{"F/X", "Move"}, {"C", "Open"}, {"Hold ESC", "Exit"}};
    }
    return {{"ESC", "Back"}, {"F/X", "Move"}, {"C", "Open"}};
}

void MenuScreen::build_content(lv_obj_t* content) {
    auto* list = lv_obj_create(content);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, kRowWidth, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 1, 0);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    /* A short menu sits at the top; a full one fills the page either way. */
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 2);

    const lv_font_t* title_font = assets().load_standard_font(12);
    if (!title_font) {
        title_font = &lv_font_montserrat_12;
    }
    const lv_font_t* detail_font = assets().load_standard_font(11, app::StandardFontWeight::Regular);
    if (!detail_font) {
        detail_font = &lv_font_montserrat_12;
    }

    std::size_t count = 0;
    const char* const* titles = titles_of(page_, count);
    rows_.resize(count);
    for (std::size_t index = 0; index < count; ++index) {
        auto& row = rows_[index];
        row.container = lv_obj_create(list);
        lv_obj_remove_style_all(row.container);
        lv_obj_set_size(row.container, LV_PCT(100), kRowHeight);
        lv_obj_set_style_radius(row.container, 3, 0);
        lv_obj_clear_flag(row.container, LV_OBJ_FLAG_SCROLLABLE);

        row.title = lv_label_create(row.container);
        lv_label_set_text(row.title, titles[index]);
        lv_obj_set_style_text_font(row.title, title_font, 0);
        lv_obj_align(row.title, LV_ALIGN_LEFT_MID, 8, 0);

        row.detail = lv_label_create(row.container);
        lv_label_set_text(row.detail, "");
        lv_obj_set_style_text_font(row.detail, detail_font, 0);
        lv_obj_align(row.detail, LV_ALIGN_RIGHT_MID, -8, 0);
    }

    /* Both observers are bound to the list object and fire once on registration. */
    lv_subject_add_observer_obj(view_model().menu_selection_subject(), refresh_observer_cb, list, this);
    lv_subject_add_observer_obj(view_model().dark_mode_subject(), refresh_observer_cb, list, this);
}

bool MenuScreen::on_key(uint32_t key) {
    /* F / X move, C or Enter opens; left does nothing. */
    if (is_up_key(key)) {
        view_model().menu_previous();
        return true;
    }
    if (is_down_key(key)) {
        view_model().menu_next();
        return true;
    }
    if (is_right_key(key) || is_ok_key(key)) {
        view_model().menu_activate();
        return true;
    }
    return false;
}

void MenuScreen::refresh() {
    const auto colors = view::palette(view_model().is_dark_mode());
    const auto selected =
        static_cast<std::size_t>(lv_subject_get_int(view_model().menu_selection_subject()));
    const bool main_menu = page_ == model::AppPage::Menu;

    for (std::size_t index = 0; index < rows_.size(); ++index) {
        auto& row = rows_[index];
        if (!row.container) {
            continue;
        }

        lv_obj_set_style_bg_color(row.container, colors.primary, 0);
        lv_obj_set_style_bg_opa(row.container, index == selected ? LV_OPA_30 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(row.title, colors.text, 0);
        lv_obj_set_style_text_color(row.detail, colors.primary, 0);

        if (main_menu && static_cast<model::MainMenuItem>(index) == model::MainMenuItem::DarkMode) {
            lv_label_set_text(row.detail, view_model().is_dark_mode() ? "On" : "Off");
        }
        else {
            lv_label_set_text(row.detail, "");
        }
    }
}

void MenuScreen::refresh_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<MenuScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen
