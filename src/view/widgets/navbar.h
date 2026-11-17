/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_widget.h"
#include "base_viewmodel.h"

#include <vector>

namespace app {
class AssetManager;
}

namespace view::widgets {

/* One "key: action" pair shown in the bottom bar. Both strings must outlive the bar. */
struct NavHint {
    const char* key;
    const char* action;
};

/* Bottom bar listing the keys the current page reacts to. It is display-only. */
class NavBar : public BaseWidgets {
public:
    NavBar(lv_obj_t* parent,
           viewmodel::BaseViewModel& view_model,
           app::AssetManager& assets,
           std::vector<NavHint> hints);

    void build() override;

private:
    void apply_theme();
    static void theme_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    viewmodel::BaseViewModel& view_model_;
    app::AssetManager& assets_;
    std::vector<NavHint> hints_;
    std::vector<lv_obj_t*> key_labels_;
};

} // namespace view::widgets
