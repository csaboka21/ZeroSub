/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

#include <array>

namespace screen {

/*
 * Creates a new fixed-code remote: a short form with the protocol, frequency,
 * modulation and key. Saving writes a .sub key file and opens its summary.
 */
class AddManuallyScreen : public BaseScreen {
public:
    AddManuallyScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

private:
    struct Row {
        lv_obj_t* container{nullptr};
        lv_obj_t* label{nullptr};
        lv_obj_t* value{nullptr};
    };

    void build_content(lv_obj_t* content) override;
    std::vector<view::widgets::NavHint> nav_hints() const override;
    bool on_key(uint32_t key) override;

    void refresh();
    static void refresh_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    std::array<Row, viewmodel::BaseViewModel::kAddFieldCount> rows_{};
    lv_obj_t* keys_label_{nullptr};
    lv_obj_t* message_label_{nullptr};
};

} // namespace screen
