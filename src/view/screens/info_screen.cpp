/*
 * SPDX-License-Identifier: MIT
 */

#include "info_screen.h"

#include "asset_manager.h"
#include "page_keys.h"
#include "theme.h"

namespace screen {
namespace {

using viewmodel::BaseViewModel;

constexpr int32_t kSideMargin = 8;
constexpr int32_t kTextWidth = view::kScreenWidth - 2 * kSideMargin;
constexpr int32_t kLabelWidth = 104;
constexpr int32_t kFirstRowY = 20;
constexpr int32_t kRowPitch = 14;
constexpr int32_t kMessageY = 94;

} // namespace

InfoScreen::InfoScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets),
      source_(view_model.info_source()),
      can_send_(view_model.info_can_send()) {
    init();
}

std::vector<view::widgets::NavHint> InfoScreen::nav_hints() const {
    switch (source_) {
        case BaseViewModel::InfoSource::SavedFile:
            if (!can_send_) {
                return {{"ESC", "Back"}, {"8", "Delete"}};
            }
            return {{"ESC", "Back"}, {"ENTER", "Send"}, {"8", "Delete"}};
        case BaseViewModel::InfoSource::ReadKey:
            if (!can_send_) {
                return {{"ESC", "Back"}, {"5", "Save"}};
            }
            return {{"ESC", "Back"}, {"ENTER", "Send"}, {"5", "Save"}};
        case BaseViewModel::InfoSource::IrLearned:
            if (!can_send_) {
                return {{"ESC", "Back"}, {"5", "Save"}};
            }
            return {{"ESC", "Back"}, {"ENTER", "Send"}, {"5", "Save"}};
        case BaseViewModel::InfoSource::IrButton:
            if (!can_send_) {
                return {{"ESC", "Back"}, {"8", "Delete"}};
            }
            return {{"ESC", "Back"}, {"ENTER", "Send"}, {"8", "Delete"}};
        case BaseViewModel::InfoSource::RawRecording:
        case BaseViewModel::InfoSource::None:
            break;
    }
    /* An unsaved recording can only be saved; it is sent from Saved afterwards. */
    return {{"ESC", "Back"}, {"5", "Save"}};
}

bool InfoScreen::on_key(uint32_t key) {
    /* Enter sends, 5 saves, 8 deletes. The view model ignores what does not apply to the source. */
    if (is_ok_key(key)) {
        view_model().info_send();
        return true;
    }
    if (key == '5') {
        view_model().info_save();
        return true;
    }
    if (key == '8') {
        view_model().info_delete();
        return true;
    }
    return false;
}

void InfoScreen::build_content(lv_obj_t* content) {
    const lv_font_t* bold_font = assets().load_standard_font(12);
    if (!bold_font) {
        bold_font = &lv_font_montserrat_12;
    }
    const lv_font_t* small_font = assets().load_standard_font(11, app::StandardFontWeight::Regular);
    if (!small_font) {
        small_font = &lv_font_montserrat_12;
    }

    title_label_ = lv_label_create(content);
    lv_label_set_long_mode(title_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title_label_, kTextWidth);
    lv_obj_set_style_text_font(title_label_, bold_font, 0);
    lv_obj_align(title_label_, LV_ALIGN_TOP_LEFT, kSideMargin, 3);

    for (std::size_t index = 0; index < rows_.size(); ++index) {
        const int32_t y = kFirstRowY + static_cast<int32_t>(index) * kRowPitch;
        auto& row = rows_[index];

        row.label = lv_label_create(content);
        lv_obj_set_style_text_font(row.label, small_font, 0);
        lv_obj_align(row.label, LV_ALIGN_TOP_LEFT, kSideMargin, y);

        row.value = lv_label_create(content);
        lv_label_set_long_mode(row.value, LV_LABEL_LONG_DOT);
        lv_obj_set_width(row.value, kTextWidth - kLabelWidth);
        lv_obj_set_style_text_font(row.value, bold_font, 0);
        lv_obj_align(row.value, LV_ALIGN_TOP_LEFT, kSideMargin + kLabelWidth, y);
    }

    message_label_ = lv_label_create(content);
    lv_label_set_long_mode(message_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(message_label_, kTextWidth);
    lv_obj_set_style_text_font(message_label_, small_font, 0);
    lv_obj_align(message_label_, LV_ALIGN_TOP_LEFT, kSideMargin, kMessageY);

    /* All observers are bound to the message label and fire once on registration. */
    for (auto* subject : {view_model().info_revision_subject(),
                          view_model().info_message_subject(),
                          view_model().dark_mode_subject()}) {
        lv_subject_add_observer_obj(subject, refresh_observer_cb, message_label_, this);
    }
}

void InfoScreen::refresh() {
    if (!message_label_) {
        return;
    }

    const auto colors = view::palette(view_model().is_dark_mode());

    lv_label_set_text(title_label_, view_model().info_title().c_str());
    lv_obj_set_style_text_color(title_label_, colors.primary, 0);

    for (std::size_t index = 0; index < rows_.size(); ++index) {
        const auto* row = view_model().info_row(index);
        lv_label_set_text(rows_[index].label, row ? row->label.c_str() : "");
        lv_label_set_text(rows_[index].value, row ? row->value.c_str() : "");
        lv_obj_set_style_text_color(rows_[index].label, colors.text_disabled, 0);
        lv_obj_set_style_text_color(rows_[index].value, colors.text, 0);
    }

    lv_label_set_text(message_label_, lv_subject_get_string(view_model().info_message_subject()));
    lv_obj_set_style_text_color(message_label_, colors.primary, 0);
}

void InfoScreen::refresh_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<InfoScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen
