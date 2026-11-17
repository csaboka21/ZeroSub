/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

#include <array>

namespace screen {

/*
 * Web Files: the address to open in a browser, the PIN to log in with, and
 * what browsers last did. The server runs while this page is open.
 */
class WebScreen : public BaseScreen {
public:
    WebScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

private:
    struct Row {
        lv_obj_t* label{nullptr};
        lv_obj_t* value{nullptr};
    };

    void build_content(lv_obj_t* content) override;
    std::vector<view::widgets::NavHint> nav_hints() const override;
    bool on_key(uint32_t key) override;

    void refresh();
    static void refresh_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    lv_obj_t* url_label_{nullptr};
    lv_obj_t* hint_label_{nullptr};
    lv_obj_t* pin_caption_{nullptr};
    lv_obj_t* pin_label_{nullptr};
    /* Folder, browsers, last event. */
    std::array<Row, 3> rows_{};
};

} // namespace screen
