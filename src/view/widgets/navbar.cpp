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

namespace {

constexpr int32_t kSidePad = 10;
constexpr int32_t kItemGap = 3;
constexpr int32_t kMinColumnGap = 8;

int32_t text_width(const char* text, const lv_font_t* font) {
    lv_point_t size{};
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

} // namespace

void NavBar::build() {
    if (core_obj_) {
        return;
    }

    core_obj_ = lv_obj_create(parent_);
    lv_obj_remove_style_all(core_obj_);
    lv_obj_set_size(core_obj_, LV_PCT(100), kNavBarHeight);
    lv_obj_align(core_obj_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(core_obj_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_hor(core_obj_, kSidePad, 0);
    reactive::bind_theme(core_obj_, view_model_.dark_mode_subject(), reactive::ThemeRole::Bar);

    populate();

    /* Bound to the bar object, so the observer goes away with it. */
    lv_subject_add_observer_obj(view_model_.dark_mode_subject(), theme_observer_cb, core_obj_, this);
}

void NavBar::set_hints(std::vector<NavHint> hints) {
    hints_ = std::move(hints);
    if (!core_obj_) {
        return;
    }
    lv_obj_clean(core_obj_);
    key_labels_.clear();
    populate();
}

void NavBar::populate() {
    const lv_font_t* key_font = assets_.load_standard_font(12);
    if (!key_font) {
        key_font = &lv_font_montserrat_12;
    }
    const lv_font_t* text_font = assets_.load_standard_font(12, app::StandardFontWeight::Regular);
    if (!text_font) {
        text_font = &lv_font_montserrat_12;
    }

    /* Measures the hints in the normal font; a page with more keys than fit wraps onto two lines. */
    int32_t needed = 0;
    for (const auto& hint : hints_) {
        needed += text_width(hint.key, key_font) + kItemGap + text_width(hint.action, text_font);
    }
    if (hints_.size() > 1) {
        needed += kMinColumnGap * static_cast<int32_t>(hints_.size() - 1);
    }
    const bool wrap = needed > kScreenWidth - 2 * kSidePad;

    if (wrap) {
        if (const auto* small = assets_.load_standard_font(11)) {
            key_font = small;
        }
        if (const auto* small = assets_.load_standard_font(11, app::StandardFontWeight::Regular)) {
            text_font = small;
        }
        lv_obj_set_flex_flow(core_obj_, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(core_obj_,
                              LV_FLEX_ALIGN_SPACE_EVENLY,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(core_obj_, kMinColumnGap, 0);
        lv_obj_set_style_pad_row(core_obj_, 0, 0);
    }
    else {
        lv_obj_set_flex_flow(core_obj_, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(core_obj_,
                              LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(core_obj_, 0, 0);
    }

    for (const auto& hint : hints_) {
        auto* item = lv_obj_create(core_obj_);
        lv_obj_remove_style_all(item);
        lv_obj_set_size(item, LV_SIZE_CONTENT, wrap ? LV_SIZE_CONTENT : LV_PCT(100));
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(item, kItemGap, 0);
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
