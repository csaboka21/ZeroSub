/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

#include <array>
#include <cstddef>

namespace screen {

/*
 * Summary of one signal: a key decoded on the Read page, the stopped Read RAW
 * recording, or a saved .sub file. ESC returns to the page it was opened from.
 */
class InfoScreen : public BaseScreen {
public:
    InfoScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

private:
    static constexpr std::size_t kMaxRows = 5;

    struct Row {
        lv_obj_t* label{nullptr};
        lv_obj_t* value{nullptr};
    };

    void build_content(lv_obj_t* content) override;
    std::vector<view::widgets::NavHint> nav_hints() const override;
    bool on_key(uint32_t key) override;

    void refresh();
    static void refresh_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    /* What is being shown is fixed for the lifetime of the screen. */
    const viewmodel::BaseViewModel::InfoSource source_;
    /* False for a rolling code: the Send hint is left out. */
    const bool can_send_;
    lv_obj_t* title_label_{nullptr};
    std::array<Row, kMaxRows> rows_{};
    lv_obj_t* message_label_{nullptr};
};

} // namespace screen
