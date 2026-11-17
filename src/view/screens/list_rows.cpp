/*
 * SPDX-License-Identifier: MIT
 */

#include "list_rows.h"

namespace screen {
namespace {

constexpr int32_t kSideMargin = 8;
constexpr int32_t kRowWidth = view::kScreenWidth - 2 * kSideMargin;

} // namespace

void ListRows::build(lv_obj_t* parent, int32_t y, const lv_font_t* font) {
    for (std::size_t row = 0; row < kVisibleRows; ++row) {
        auto* container = lv_obj_create(parent);
        lv_obj_remove_style_all(container);
        lv_obj_set_size(container, kRowWidth, kRowHeight - 1);
        lv_obj_align(container,
                     LV_ALIGN_TOP_LEFT,
                     kSideMargin,
                     y + static_cast<int32_t>(row) * kRowHeight);
        lv_obj_set_style_radius(container, 3, 0);
        lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);

        auto* label = lv_label_create(container);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(label, kRowWidth - 12);
        lv_obj_set_style_text_font(label, font, 0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 6, 0);
        lv_label_set_text(label, "");

        containers_[row] = container;
        labels_[row] = label;
    }
}

void ListRows::render(std::size_t count,
                      std::size_t selected,
                      const std::function<std::string(std::size_t)>& text,
                      const view::ThemePalette& colors) {
    /* Keep one row of context above the selection where possible. */
    std::size_t first = selected > 0 ? selected - 1 : 0;
    if (count <= kVisibleRows) {
        first = 0;
    }
    else if (first > count - kVisibleRows) {
        first = count - kVisibleRows;
    }

    for (std::size_t row = 0; row < kVisibleRows; ++row) {
        if (!containers_[row]) {
            continue;
        }

        const std::size_t index = first + row;
        if (index >= count) {
            lv_obj_add_flag(containers_[row], LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        lv_obj_remove_flag(containers_[row], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(labels_[row], text(index).c_str());
        lv_obj_set_style_text_color(labels_[row], colors.text, 0);
        lv_obj_set_style_bg_color(containers_[row], colors.primary, 0);
        lv_obj_set_style_bg_opa(containers_[row], index == selected ? LV_OPA_30 : LV_OPA_TRANSP, 0);
    }
}

} // namespace screen
