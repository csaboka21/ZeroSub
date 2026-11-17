/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "list_rows.h"

namespace screen {

/*
 * The three list pages of the Infrared tool, which share one layout: a list,
 * a line describing the selected row, and a message line. `page` says which:
 * Learn (signals received), Saved Remotes (folders and .ir files) or Remote
 * (the buttons of one file).
 */
class InfraredScreen : public BaseScreen {
public:
    InfraredScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, model::AppPage page);

private:
    void build_content(lv_obj_t* content) override;
    std::vector<view::widgets::NavHint> nav_hints() const override;
    bool on_key(uint32_t key) override;

    void refresh();
    static void refresh_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    const model::AppPage page_;
    ListRows rows_;
    lv_obj_t* empty_label_{nullptr};
    lv_obj_t* summary_label_{nullptr};
    lv_obj_t* info_label_{nullptr};
};

} // namespace screen
