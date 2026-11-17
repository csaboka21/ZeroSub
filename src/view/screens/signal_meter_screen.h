/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

namespace screen {

/* Live RSSI on one frequency, with a selectable modulation preset. */
class SignalMeterScreen : public BaseScreen {
public:
    SignalMeterScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

private:
    void build_content(lv_obj_t* content) override;
    std::vector<view::widgets::NavHint> nav_hints() const override;
    bool on_key(uint32_t key) override;
};

} // namespace screen
