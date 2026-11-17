/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lvgl.h"
#include "theme.h"

#include <array>
#include <cstddef>
#include <functional>
#include <string>

namespace screen {

/*
 * A fixed window of text rows over a longer list, with one highlighted row.
 * The window scrolls so the selected item stays visible.
 */
class ListRows {
public:
    static constexpr std::size_t kVisibleRows = 4;
    static constexpr int32_t kRowHeight = 17;

    void build(lv_obj_t* parent, int32_t y, const lv_font_t* font);
    void render(std::size_t count,
                std::size_t selected,
                const std::function<std::string(std::size_t)>& text,
                const view::ThemePalette& colors);

private:
    std::array<lv_obj_t*, kVisibleRows> containers_{};
    std::array<lv_obj_t*, kVisibleRows> labels_{};
};

} // namespace screen
