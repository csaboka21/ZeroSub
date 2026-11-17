/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "navbar.h"
#include "base_viewmodel.h"
#include "titlebar.h"
#include "lvgl.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace app {
class AssetManager;
}

namespace screen {

class BaseScreen {
public:
    BaseScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    virtual ~BaseScreen();

    BaseScreen(const BaseScreen&) = delete;
    BaseScreen& operator=(const BaseScreen&) = delete;

    /* Builds the screen. Call from the most-derived constructor, since it uses the virtuals below. */
    void init();
    lv_obj_t* root() const;

protected:
    virtual void build_content(lv_obj_t* content) = 0;
    /* Key hints for the bottom bar. */
    virtual std::vector<view::widgets::NavHint> nav_hints() const = 0;
    /* Handles a key press for this page. Returns true if the key was used. */
    virtual bool on_key(uint32_t key);
    /* Rebuilds the bottom bar from nav_hints(), for pages whose keys change with their state. */
    void refresh_nav_hints();

    viewmodel::BaseViewModel& view_model();
    app::AssetManager& assets();

private:
    static bool page_key_listener(uint32_t key, void* user_data);

    viewmodel::BaseViewModel& view_model_;
    app::AssetManager& assets_;
    lv_obj_t* root_{nullptr};
    lv_obj_t* content_{nullptr};
    std::unique_ptr<view::widgets::TitleBar> title_bar_;
    std::unique_ptr<view::widgets::NavBar> nav_bar_;
};

} // namespace screen
