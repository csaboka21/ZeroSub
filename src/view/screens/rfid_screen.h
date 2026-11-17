/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "list_rows.h"

namespace screen {

/*
 * The three pages of the 125 kHz RFID tool, which share the NFC pages'
 * layout: a few rows, a status line, and a message line. `page` says which:
 * Saved Keys (folders and .rfid files), Key (one saved key, to emulate) or
 * Add Manually (a new EM4100 ID).
 */
class RfidScreen : public BaseScreen {
public:
    RfidScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, model::AppPage page);

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
