/*
 * SPDX-License-Identifier: MIT
 */

#include "add_manually_screen.h"

#include "asset_manager.h"
#include "page_keys.h"
#include "theme.h"

namespace screen {
namespace {

using viewmodel::BaseViewModel;

constexpr int32_t kSideMargin = 8;
constexpr int32_t kRowWidth = view::kScreenWidth - 2 * kSideMargin;
constexpr int32_t kRowHeight = 17;
constexpr int32_t kRowPitch = 18;
constexpr int32_t kFirstRowY = 3;
constexpr int32_t kValueX = 96;
constexpr int32_t kKeysY = 78;
constexpr int32_t kMessageY = 94;

constexpr std::array<const char*, BaseViewModel::kAddFieldCount> kLabels = {
    "Protocol",
    "Frequency",
    "Modulation",
    "Key",
};

} // namespace

AddManuallyScreen::AddManuallyScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

std::vector<view::widgets::NavHint> AddManuallyScreen::nav_hints() const {
    return {{"ESC", "Back"}, {"F/X", "Field"}, {"Z/C", "Change"}, {"ENTER", "Save"}};
}

bool AddManuallyScreen::on_key(uint32_t key) {
    if (is_up_key(key)) {
        view_model().add_field_previous();
        return true;
    }
    if (is_down_key(key)) {
        view_model().add_field_next();
        return true;
    }
    if (is_left_key(key)) {
        view_model().add_value_previous();
        return true;
    }
    if (is_right_key(key)) {
        view_model().add_value_next();
        return true;
    }
    if (is_ok_key(key)) {
        view_model().add_save();
        return true;
    }
    switch (key) {
        case '6':
            view_model().add_randomize();
            return true;
        case '7':
            view_model().add_digit_step(false);
            return true;
        case '8':
            view_model().add_digit_step(true);
            return true;
        default:
            return false;
    }
}

void AddManuallyScreen::build_content(lv_obj_t* content) {
    const lv_font_t* bold_font = assets().load_standard_font(12);
    if (!bold_font) {
        bold_font = &lv_font_montserrat_12;
    }
    const lv_font_t* small_font = assets().load_standard_font(11, app::StandardFontWeight::Regular);
    if (!small_font) {
        small_font = &lv_font_montserrat_12;
    }

    for (std::size_t index = 0; index < rows_.size(); ++index) {
        auto& row = rows_[index];
        row.container = lv_obj_create(content);
        lv_obj_remove_style_all(row.container);
        lv_obj_set_size(row.container, kRowWidth, kRowHeight);
        lv_obj_align(row.container, LV_ALIGN_TOP_LEFT, kSideMargin,
                     kFirstRowY + static_cast<int32_t>(index) * kRowPitch);
        lv_obj_set_style_radius(row.container, 3, 0);
        lv_obj_clear_flag(row.container, LV_OBJ_FLAG_SCROLLABLE);

        row.label = lv_label_create(row.container);
        lv_label_set_text(row.label, kLabels[index]);
        lv_obj_set_style_text_font(row.label, small_font, 0);
        lv_obj_align(row.label, LV_ALIGN_LEFT_MID, 6, 0);

        row.value = lv_label_create(row.container);
        lv_label_set_long_mode(row.value, LV_LABEL_LONG_DOT);
        lv_obj_set_width(row.value, kRowWidth - kValueX - 6);
        lv_obj_set_style_text_font(row.value, bold_font, 0);
        lv_obj_align(row.value, LV_ALIGN_LEFT_MID, kValueX, 0);
    }

    keys_label_ = lv_label_create(content);
    lv_label_set_long_mode(keys_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(keys_label_, kRowWidth);
    lv_obj_set_style_text_font(keys_label_, small_font, 0);
    lv_obj_align(keys_label_, LV_ALIGN_TOP_LEFT, kSideMargin, kKeysY);

    message_label_ = lv_label_create(content);
    lv_label_set_long_mode(message_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(message_label_, kRowWidth);
    lv_obj_set_style_text_font(message_label_, small_font, 0);
    lv_obj_align(message_label_, LV_ALIGN_TOP_LEFT, kSideMargin, kMessageY);

    /* All observers are bound to the message label and fire once on registration. */
    for (auto* subject : {view_model().add_revision_subject(),
                          view_model().add_message_subject(),
                          view_model().dark_mode_subject()}) {
        lv_subject_add_observer_obj(subject, refresh_observer_cb, message_label_, this);
    }
}

void AddManuallyScreen::refresh() {
    if (!message_label_) {
        return;
    }

    const auto colors = view::palette(view_model().is_dark_mode());
    const auto selected = static_cast<std::size_t>(view_model().add_field());

    for (std::size_t index = 0; index < rows_.size(); ++index) {
        auto& row = rows_[index];
        const auto field = static_cast<BaseViewModel::AddField>(index);
        lv_label_set_text(row.value, view_model().add_field_value(field).c_str());
        lv_obj_set_style_bg_color(row.container, colors.primary, 0);
        lv_obj_set_style_bg_opa(row.container, index == selected ? LV_OPA_30 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(row.label, colors.text_disabled, 0);
        lv_obj_set_style_text_color(row.value, colors.text, 0);
    }

    /* The keys that act on the selected field. */
    lv_label_set_text(keys_label_,
                      view_model().add_field() == BaseViewModel::AddField::Key
                          ? "Z/C Digit   7/8 Change digit   6 New random key"
                          : "Z/C Change   6 New random key   ENTER Save");
    lv_obj_set_style_text_color(keys_label_, colors.text, 0);

    lv_label_set_text(message_label_, lv_subject_get_string(view_model().add_message_subject()));
    lv_obj_set_style_text_color(message_label_, colors.warning, 0);
}

void AddManuallyScreen::refresh_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<AddManuallyScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen
