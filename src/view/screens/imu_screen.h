/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

#include <array>

namespace screen {

/*
 * The three pages of the IMU tool. `page` says which: Motion (BMI270
 * acceleration, rotation, tilt and temperature), Compass (BMM150 field and
 * heading, with a dial) or Level (a bubble level). Each ends with a status
 * line saying where the chips were found and a message line.
 */
class ImuScreen : public BaseScreen {
public:
    ImuScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, model::AppPage page);

private:
    void build_content(lv_obj_t* content) override;
    std::vector<view::widgets::NavHint> nav_hints() const override;
    bool on_key(uint32_t key) override;

    void build_motion(lv_obj_t* content);
    void build_dial(lv_obj_t* content);
    void refresh();
    void refresh_motion();
    void refresh_compass();
    void refresh_level();
    static void refresh_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    lv_obj_t* make_label(lv_obj_t* parent, const lv_font_t* font, int32_t x, int32_t y, int32_t width,
                         lv_text_align_t align);

    const model::AppPage page_;
    const lv_font_t* small_font_{nullptr};
    const lv_font_t* bold_font_{nullptr};
    const lv_font_t* large_font_{nullptr};

    /* Motion: row captions, the X / Y / Z header and a 3 x 3 grid of values. */
    std::array<lv_obj_t*, 3> captions_{};
    std::array<lv_obj_t*, 3> headers_{};
    std::array<std::array<lv_obj_t*, 3>, 3> values_{};
    lv_obj_t* tilt_label_{nullptr};

    /* Compass and Level: a round dial on the left, readouts on the right. */
    lv_obj_t* dial_{nullptr};
    lv_obj_t* needle_{nullptr};
    std::array<lv_point_precise_t, 2> needle_points_{};
    lv_obj_t* north_label_{nullptr};
    lv_obj_t* cross_h_{nullptr};
    lv_obj_t* cross_v_{nullptr};
    std::array<lv_point_precise_t, 2> cross_h_points_{};
    std::array<lv_point_precise_t, 2> cross_v_points_{};
    lv_obj_t* bubble_{nullptr};
    lv_obj_t* big_label_{nullptr};
    lv_obj_t* detail_label_{nullptr};

    lv_obj_t* status_label_{nullptr};
    lv_obj_t* message_label_{nullptr};
};

} // namespace screen
