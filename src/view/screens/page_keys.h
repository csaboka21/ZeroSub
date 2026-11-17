/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lvgl.h"

#include <cstdint>

namespace screen {

/*
 * Shared key roles. Navigation uses four letter keys as an arrow cluster:
 *
 *        F                up
 *     Z  X  C       left down right
 *
 * Up / down move through a list or change the modulation, left / right change
 * the frequency, and right opens the selected item. Enter confirms an action
 * (record, stop, save). The real arrow keys do the same as the letters.
 */
inline bool is_up_key(uint32_t key) {
    return key == LV_KEY_UP || key == 'f' || key == 'F';
}

inline bool is_down_key(uint32_t key) {
    return key == LV_KEY_DOWN || key == 'x' || key == 'X';
}

inline bool is_left_key(uint32_t key) {
    return key == LV_KEY_LEFT || key == 'z' || key == 'Z';
}

inline bool is_right_key(uint32_t key) {
    return key == LV_KEY_RIGHT || key == 'c' || key == 'C';
}

inline bool is_ok_key(uint32_t key) {
    return key == LV_KEY_ENTER;
}

} // namespace screen
