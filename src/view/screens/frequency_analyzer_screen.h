/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

#include <array>

namespace screen {

/*
 * Sweeps the frequency list and shows the strongest channel, plus one level
 * bar per channel from the last completed sweep.
 */
class FrequencyAnalyzerScreen : public BaseScreen {
public:
    FrequencyAnalyzerScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

private:
    void build_content(lv_obj_t* content) override;
    std::vector<view::widgets::NavHint> nav_hints() const override;

    void refresh();
    static void refresh_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    lv_obj_t* peak_label_{nullptr};
    std::array<lv_obj_t*, viewmodel::BaseViewModel::kAnalyzerChannelCount> bars_{};
};

} // namespace screen
