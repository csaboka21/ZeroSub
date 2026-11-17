/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

#include <vector>

namespace screen {

/*
 * A menu: one row per entry. Used for the main menu (one row per tool, plus
 * settings) and for each tool's own menu; `page` says which one to show.
 */
class MenuScreen : public BaseScreen {
public:
    MenuScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, model::AppPage page);

private:
    struct Row {
        lv_obj_t* container{nullptr};
        lv_obj_t* title{nullptr};
        lv_obj_t* detail{nullptr};
    };

    void build_content(lv_obj_t* content) override;
    std::vector<view::widgets::NavHint> nav_hints() const override;
    bool on_key(uint32_t key) override;

    void refresh();
    static void refresh_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    const model::AppPage page_;
    std::vector<Row> rows_;
};

} // namespace screen
