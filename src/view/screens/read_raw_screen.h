/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

namespace screen {

/*
 * Records the raw pulse train on one frequency: a scrolling RSSI graph with
 * the recording threshold, the recording state, and the keys for that state.
 */
class ReadRawScreen : public BaseScreen {
public:
    ReadRawScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

private:
    void build_content(lv_obj_t* content) override;
    std::vector<view::widgets::NavHint> nav_hints() const override;
    bool on_key(uint32_t key) override;

    model::RawState state();
    void refresh();
    void plot_reading();
    static void refresh_observer_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void tick_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    lv_obj_t* chart_{nullptr};
    lv_chart_series_t* series_{nullptr};
    lv_obj_t* threshold_line_{nullptr};
    lv_obj_t* threshold_label_{nullptr};
    lv_obj_t* state_label_{nullptr};
    lv_obj_t* keys_label_{nullptr};
    lv_obj_t* info_label_{nullptr};
};

} // namespace screen
