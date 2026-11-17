/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "navbar.h"

#include "asset_manager.h"
#include "bindings.h"
#include "theme.h"

#include <utility>

namespace view::widgets {

NavBar::NavBar(lv_obj_t* parent,
               viewmodel::BaseViewModel& view_model,
               app::AssetManager& assets,
               std::vector<NavHint> hints)
    : BaseWidgets(parent), view_model_(view_model), assets_(assets), hints_(std::move(hints)) {}

void NavBar::build() {
    if (core_obj_) {
        return;
    }

    core_obj_ = lv_obj_create(parent_);
    lv_obj_remove_style_all(core_obj_);
    lv_obj_set_size(core_obj_, LV_PCT(100), kNavBarHeight);
    lv_obj_align(core_obj_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(core_obj_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(core_obj_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(core_obj_,
                          LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(core_obj_, 10, 0);
    reactive::bind_theme(core_obj_, view_model_.dark_mode_subject(), reactive::ThemeRole::Bar);

    const lv_font_t* key_font = assets_.load_standard_font(12);
    if (!key_font) {
        key_font = &lv_font_montserrat_12;
    }
    const lv_font_t* text_font = assets_.load_standard_font(12, app::StandardFontWeight::Regular);
    if (!text_font) {
        text_font = &lv_font_montserrat_12;
    }

    for (const auto& hint : hints_) {
        auto* item = lv_obj_create(core_obj_);
        lv_obj_remove_style_all(item);
        lv_obj_set_size(item, LV_SIZE_CONTENT, LV_PCT(100));
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(item, 3, 0);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

        auto* key = lv_label_create(item);
        lv_label_set_text(key, hint.key);
        lv_obj_set_style_text_font(key, key_font, 0);
        key_labels_.push_back(key);

        auto* action = lv_label_create(item);
        lv_label_set_text(action, hint.action);
        lv_obj_set_style_text_font(action, text_font, 0);
        reactive::bind_theme(action, view_model_.dark_mode_subject(), reactive::ThemeRole::Text);
    }

    /* Bound to the bar object, so the observer goes away with it. */
    lv_subject_add_observer_obj(view_model_.dark_mode_subject(), theme_observer_cb, core_obj_, this);
    apply_theme();
}

void NavBar::apply_theme() {
    const auto colors = view::palette(view_model_.is_dark_mode());
    for (auto* key : key_labels_) {
        lv_obj_set_style_text_color(key, colors.primary, 0);
    }
}

void NavBar::theme_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* nav_bar = static_cast<NavBar*>(lv_observer_get_user_data(observer));
    if (nav_bar) {
        nav_bar->apply_theme();
    }
}

} // namespace view::widgets
