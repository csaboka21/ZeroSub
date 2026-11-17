/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "list_rows.h"

namespace screen {

/* Listens on one frequency and lists the fixed-code keys it decodes, newest first. */
class ReadScreen : public BaseScreen {
public:
    ReadScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

private:
    void build_content(lv_obj_t* content) override;
    std::vector<view::widgets::NavHint> nav_hints() const override;
    bool on_key(uint32_t key) override;

    void refresh();
    static void refresh_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    ListRows rows_;
    lv_obj_t* empty_label_{nullptr};
    lv_obj_t* info_label_{nullptr};
};

} // namespace screen
