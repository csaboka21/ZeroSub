/*
 * SPDX-License-Identifier: MIT
 */

#include "nfc_screen.h"

#include "asset_manager.h"
#include "page_keys.h"

namespace screen {
namespace {

constexpr int32_t kSideMargin = 8;
constexpr int32_t kListY = 4;
constexpr int32_t kTextWidth = view::kScreenWidth - 2 * kSideMargin;

const char* empty_text(model::AppPage page) {
    switch (page) {
        case model::AppPage::NfcSaved:
            return "This folder is empty.";
        case model::AppPage::NfcCard:
            return "";
        default:
            return "No tag yet.\nHold one flat against the Cap.";
    }
}

} // namespace

NfcScreen::NfcScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, model::AppPage page)
    : BaseScreen(view_model, assets),
      page_(page),
      amiibo_(page == model::AppPage::NfcCard && view_model.nfc_card_is_amiibo()) {
    init();
}

std::vector<view::widgets::NavHint> NfcScreen::nav_hints() const {
    switch (page_) {
        case model::AppPage::NfcSaved:
            return {{"ESC", "Back"},
                    {"F/X", "Move"},
                    {"Z", "Up"},
                    {"C/ENTER", "Open"},
                    {"8", "Delete"}};
        case model::AppPage::NfcCard:
            if (amiibo_) {
                return {{"Z/ESC", "Back"},
                        {"F/X", "Scroll"},
                        {"ENTER", "Emulate"},
                        {"6", "Write"},
                        {"R", "New UID"},
                        {"O", "Orig UID"},
                        {"8", "Delete"}};
            }
            return {{"Z/ESC", "Back"},
                    {"F/X", "Scroll"},
                    {"ENTER", "Emulate"},
                    {"6", "Write"},
                    {"8", "Delete"}};
        default:
            return {{"ESC", "Back"}, {"F/X", "Scroll"}, {"ENTER", "Save"}, {"5", "Again"}};
    }
}

bool NfcScreen::on_key(uint32_t key) {
    /* Up / down select a file on Saved Tags and scroll the tag's lines elsewhere. */
    if (is_up_key(key)) {
        view_model().nfc_previous();
        return true;
    }
    if (is_down_key(key)) {
        view_model().nfc_next();
        return true;
    }
    if (page_ == model::AppPage::NfcSaved) {
        if (is_right_key(key)) {
            view_model().nfc_open();
            return true;
        }
        if (is_left_key(key)) {
            /* Left goes up a folder, as ESC does, but never leaves the tool. */
            view_model().nfc_up();
            return true;
        }
    }
    if (is_ok_key(key)) {
        view_model().nfc_activate();
        return true;
    }
    if (page_ == model::AppPage::NfcCard) {
        if (is_left_key(key)) {
            view_model().go_back();
            return true;
        }
        if (key == '6') {
            view_model().nfc_write();
            return true;
        }
        if (key == 'r' || key == 'R') {
            view_model().nfc_new_uid();
            return true;
        }
        if (key == 'o' || key == 'O') {
            view_model().nfc_original_uid();
            return true;
        }
    }
    if (key == '8' && page_ != model::AppPage::NfcRead) {
        view_model().nfc_delete();
        return true;
    }
    if (key == '5' && page_ == model::AppPage::NfcRead) {
        view_model().nfc_read_again();
        return true;
    }
    return false;
}

void NfcScreen::build_content(lv_obj_t* content) {
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
    for (auto* subject : {view_model().nfc_revision_subject(),
                          view_model().nfc_selection_subject(),
                          view_model().nfc_message_subject(),
                          view_model().dark_mode_subject()}) {
        lv_subject_add_observer_obj(subject, refresh_observer_cb, info_label_, this);
    }
}

void NfcScreen::refresh() {
    if (!info_label_) {
        return;
    }

    const auto colors = view::palette(view_model().is_dark_mode());
    const std::size_t count = view_model().nfc_list_count(page_);
    /*
     * The file list always has a selected row. The other pages show plain
     * lines, with a cursor only when there are more of them than fit.
     */
    const bool cursor = page_ == model::AppPage::NfcSaved || count > ListRows::kVisibleRows;
    const std::size_t selected = cursor ? view_model().nfc_selection(page_) : count;

    rows_.render(count,
                 selected,
                 [this](std::size_t index) { return view_model().nfc_list_text(page_, index); },
                 colors);

    lv_obj_set_style_text_color(empty_label_, colors.text_disabled, 0);
    if (count == 0) {
        lv_obj_remove_flag(empty_label_, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(empty_label_, LV_OBJ_FLAG_HIDDEN);
    }

    lv_label_set_text(summary_label_, view_model().nfc_list_summary(page_).c_str());
    lv_obj_set_style_text_color(summary_label_, colors.primary, 0);

    lv_label_set_text(info_label_, lv_subject_get_string(view_model().nfc_message_subject()));
    lv_obj_set_style_text_color(info_label_, colors.text_disabled, 0);
}

void NfcScreen::refresh_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<NfcScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen
