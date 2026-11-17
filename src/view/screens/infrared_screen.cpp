/*
 * SPDX-License-Identifier: MIT
 */

#include "infrared_screen.h"

#include "asset_manager.h"
#include "page_keys.h"

namespace screen {
namespace {

constexpr int32_t kSideMargin = 8;
constexpr int32_t kListY = 4;
constexpr int32_t kTextWidth = view::kScreenWidth - 2 * kSideMargin;

const char* empty_text(model::AppPage page) {
    switch (page) {
        case model::AppPage::IrSaved:
            return "This folder is empty.";
        case model::AppPage::IrRemote:
            return "This remote has no buttons.";
        default:
            return "Waiting for a signal.\nPoint a remote at the receiver and press a button.";
    }
}

} // namespace

InfraredScreen::InfraredScreen(viewmodel::BaseViewModel& view_model,
                               app::AssetManager& assets,
                               model::AppPage page)
    : BaseScreen(view_model, assets), page_(page) {
    init();
}

std::vector<view::widgets::NavHint> InfraredScreen::nav_hints() const {
    switch (page_) {
        case model::AppPage::IrSaved:
            return {{"ESC", "Back"}, {"F/X", "Move"}, {"C", "Open"}, {"8", "Delete"}};
        case model::AppPage::IrRemote:
            return {{"ESC", "Back"}, {"ENTER", "Send"}, {"C", "Info"}, {"8", "Delete"}};
        default:
            return {{"ESC", "Back"}, {"C", "Info"}, {"ENTER", "Save"}, {"6", "Save all"}};
    }
}

bool InfraredScreen::on_key(uint32_t key) {
    if (is_up_key(key)) {
        view_model().ir_previous();
        return true;
    }
    if (is_down_key(key)) {
        view_model().ir_next();
        return true;
    }
    if (is_right_key(key)) {
        view_model().ir_open();
        return true;
    }
    if (is_ok_key(key)) {
        view_model().ir_activate();
        return true;
    }
    if (is_left_key(key)) {
        /* Left goes up a level, as ESC does, but never leaves the tool. */
        if (page_ == model::AppPage::IrSaved) {
            view_model().ir_up();
        }
        else if (page_ == model::AppPage::IrRemote) {
            view_model().go_back();
        }
        return true;
    }
    if (key == '8' && page_ != model::AppPage::IrLearn) {
        view_model().ir_delete();
        return true;
    }
    if (key == '6' && page_ == model::AppPage::IrLearn) {
        view_model().ir_save_all();
        return true;
    }
    return false;
}

void InfraredScreen::build_content(lv_obj_t* content) {
    const lv_font_t* bold_font = assets().load_standard_font(12);
    if (!bold_font) {
        bold_font = &lv_font_montserrat_12;
    }
    const lv_font_t* small_font = assets().load_standard_font(11, app::StandardFontWeight::Regular);
    if (!small_font) {
        small_font = &lv_font_montserrat_12;
    }

    rows_.build(content, kListY, small_font);

    empty_label_ = lv_label_create(content);
    lv_label_set_text(empty_label_, empty_text(page_));
    lv_obj_set_style_text_align(empty_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(empty_label_, small_font, 0);
    lv_obj_align(empty_label_, LV_ALIGN_TOP_MID, 0, kListY + 20);

    summary_label_ = lv_label_create(content);
    lv_label_set_long_mode(summary_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(summary_label_, kTextWidth);
    lv_obj_set_style_text_font(summary_label_, bold_font, 0);
    lv_obj_align(summary_label_, LV_ALIGN_TOP_LEFT, kSideMargin, 76);

    info_label_ = lv_label_create(content);
    lv_label_set_long_mode(info_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(info_label_, kTextWidth);
    lv_obj_set_style_text_font(info_label_, small_font, 0);
    lv_obj_align(info_label_, LV_ALIGN_TOP_LEFT, kSideMargin, 94);

    /* All observers are bound to the info label and fire once on registration. */
    for (auto* subject : {view_model().ir_revision_subject(),
                          view_model().ir_selection_subject(page_),
                          view_model().ir_message_subject(),
                          view_model().dark_mode_subject()}) {
        lv_subject_add_observer_obj(subject, refresh_observer_cb, info_label_, this);
    }
}

void InfraredScreen::refresh() {
    if (!info_label_) {
        return;
    }

    const auto colors = view::palette(view_model().is_dark_mode());
    const std::size_t count = view_model().ir_list_count(page_);
    const auto selected =
        static_cast<std::size_t>(lv_subject_get_int(view_model().ir_selection_subject(page_)));

    rows_.render(count,
                 selected,
                 [this](std::size_t index) { return view_model().ir_list_text(page_, index); },
                 colors);

    lv_obj_set_style_text_color(empty_label_, colors.text_disabled, 0);
    if (count == 0) {
        lv_obj_remove_flag(empty_label_, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(empty_label_, LV_OBJ_FLAG_HIDDEN);
    }

    lv_label_set_text(summary_label_, view_model().ir_list_summary(page_).c_str());
    lv_obj_set_style_text_color(summary_label_, colors.primary, 0);

    lv_label_set_text(info_label_, lv_subject_get_string(view_model().ir_message_subject()));
    lv_obj_set_style_text_color(info_label_, colors.text_disabled, 0);
}

void InfraredScreen::refresh_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<InfraredScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen
