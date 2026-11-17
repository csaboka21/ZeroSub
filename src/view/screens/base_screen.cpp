/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "base_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "linux_input.h"
#include "theme.h"

namespace screen {

BaseScreen::BaseScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : view_model_(view_model), assets_(assets) {}

BaseScreen::~BaseScreen() {
    /* Only clears the registration if it is still ours; the next screen may already own it. */
    platform::clear_page_key_listener(page_key_listener, this);

    title_bar_.reset();
    nav_bar_.reset();

    if (root_ && lv_obj_is_valid(root_)) {
        lv_obj_delete(root_);
    }
}

void BaseScreen::init() {
    if (root_) {
        return;
    }

    root_ = lv_obj_create(nullptr);
    lv_obj_remove_style_all(root_);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_size(root_, view::kScreenWidth, view::kScreenHeight);
    reactive::bind_theme(root_, view_model_.dark_mode_subject(), reactive::ThemeRole::Screen);

    title_bar_ = std::make_unique<view::widgets::TitleBar>(root_, view_model_, assets_);
    title_bar_->build();

    nav_bar_ = std::make_unique<view::widgets::NavBar>(root_, view_model_, assets_, nav_hints());
    nav_bar_->build();

    content_ = lv_obj_create(root_);
    lv_obj_remove_style_all(content_);
    lv_obj_set_size(content_, LV_PCT(100), view::kScreenHeight - view::kTitleBarHeight - view::kNavBarHeight);
    lv_obj_align(content_, LV_ALIGN_TOP_MID, 0, view::kTitleBarHeight);
    lv_obj_clear_flag(content_, LV_OBJ_FLAG_SCROLLABLE);
    reactive::bind_theme(content_, view_model_.dark_mode_subject(), reactive::ThemeRole::Surface);

    build_content(content_);

    platform::set_page_key_listener(page_key_listener, this);
}

lv_obj_t* BaseScreen::root() const {
    return root_;
}

bool BaseScreen::on_key(uint32_t) {
    return false;
}

viewmodel::BaseViewModel& BaseScreen::view_model() {
    return view_model_;
}

app::AssetManager& BaseScreen::assets() {
    return assets_;
}

bool BaseScreen::page_key_listener(uint32_t key, void* user_data) {
    auto* screen = static_cast<BaseScreen*>(user_data);
    return screen && screen->on_key(key);
}

} // namespace screen
