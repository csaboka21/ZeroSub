/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "help_popup.h"

#include "asset_manager.h"
#include "base_viewmodel.h"
#include "theme.h"

#include <array>
#include <cstddef>

namespace view::widgets {
namespace {

struct HelpRow {
    const char* key;
    const char* action;
};

struct HelpPage {
    const char* badge;
    std::array<HelpRow, 5> rows;
    std::size_t row_count;
};

constexpr HelpPage kMenuHelp = {
    "MENU",
    {{
        {"F / X", "Move the selection"},
        {"C / ENTER", "Open item"},
        {"HOLD ESC", "Exit"},
        {"", ""},
        {"", ""},
    }},
    3,
};

constexpr HelpPage kToolMenuHelp = {
    "SUB-GHZ",
    {{
        {"F / X", "Move the selection"},
        {"C / ENTER", "Open item"},
        {"ESC", "Back to the main menu"},
        {"", ""},
        {"", ""},
    }},
    3,
};

constexpr HelpPage kAnalyzerHelp = {
    "ANALYZER",
    {{
        {"ESC", "Back to Sub-GHz menu"},
        {"", "Scans 17 channels, 300-925 MHz"},
        {"", "Shows the strongest signal"},
        {"", ""},
        {"", ""},
    }},
    3,
};

constexpr HelpPage kMeterHelp = {
    "METER",
    {{
        {"ESC", "Back to Sub-GHz menu"},
        {"Z / C", "Previous / next frequency"},
        {"F / X", "Previous / next modulation"},
        {"", ""},
        {"", ""},
    }},
    3,
};

constexpr HelpPage kReadRawHelp = {
    "READ RAW",
    {{
        {"ENTER", "Record, stop, then save"},
        {"Z / C", "Frequency (when ready)"},
        {"F / X, 6", "Modulation, threshold"},
        {"C, 8", "Info, erase (when stopped)"},
        {"ESC", "Back (discards unsaved)"},
    }},
    5,
};

constexpr HelpPage kReadHelp = {
    "READ",
    {{
        {"F / X", "Select a decoded key"},
        {"C", "Summary of the selected key"},
        {"ENTER", "Save the selected key"},
        {"7 / 8", "Previous / next frequency"},
        {"5", "Next modulation"},
    }},
    5,
};

constexpr HelpPage kSavedHelp = {
    "SAVED",
    {{
        {"F / X", "Select a file or folder"},
        {"C", "Open folder / file summary"},
        {"ENTER", "Send the selected file"},
        {"8, then 8", "Delete the selected file"},
        {"Z / ESC", "Up one folder / stop sending"},
    }},
    5,
};

constexpr HelpPage kInfoHelp = {
    "INFO",
    {{
        {"ENTER", "Send (key or saved file)"},
        {"5", "Save (key or recording)"},
        {"8, then 8", "Delete (saved file)"},
        {"ESC", "Stop sending / back"},
        {"", "EU bands only, no rolling codes"},
    }},
    5,
};

constexpr HelpPage kAddHelp = {
    "ADD",
    {{
        {"F / X", "Select a field"},
        {"Z / C", "Change it / move key digit"},
        {"7 / 8", "Lower / raise the key digit"},
        {"6", "New random key"},
        {"ENTER", "Save and open the summary"},
    }},
    5,
};

constexpr HelpPage kIrMenuHelp = {
    "INFRARED",
    {{
        {"F / X", "Move the selection"},
        {"C / ENTER", "Open item"},
        {"ESC", "Back to the main menu"},
        {"", ""},
        {"", ""},
    }},
    3,
};

constexpr HelpPage kIrLearnHelp = {
    "LEARN",
    {{
        {"F / X", "Select a received signal"},
        {"C", "Summary of the signal"},
        {"ENTER", "Save it as a new remote"},
        {"6", "Save all as one remote"},
        {"ESC", "Back to Infrared menu"},
    }},
    5,
};

constexpr HelpPage kIrSavedHelp = {
    "REMOTES",
    {{
        {"F / X", "Select a remote or folder"},
        {"C / ENTER", "Open it"},
        {"8, then 8", "Delete the selected remote"},
        {"Z / ESC", "Up one folder / back"},
        {"", ""},
    }},
    4,
};

constexpr HelpPage kIrRemoteHelp = {
    "REMOTE",
    {{
        {"F / X", "Select a button"},
        {"ENTER", "Send the button"},
        {"C", "Summary of the button"},
        {"8, then 8", "Delete the button"},
        {"Z / ESC", "Back to the remotes"},
    }},
    5,
};

constexpr HelpPage kIrInfoHelp = {
    "INFO",
    {{
        {"ENTER", "Send the signal"},
        {"5", "Save (learned signal)"},
        {"8, then 8", "Delete (saved button)"},
        {"ESC", "Stop sending / back"},
        {"", ""},
    }},
    4,
};

constexpr HelpPage kNfcMenuHelp = {
    "NFC",
    {{
        {"F / X", "Move the selection"},
        {"C / ENTER", "Open item"},
        {"ESC", "Back to the main menu"},
        {"", ""},
        {"", ""},
    }},
    3,
};

constexpr HelpPage kNfcReadHelp = {
    "READ TAG",
    {{
        {"", "Hold a tag against the Cap"},
        {"F / X", "Scroll the tag's lines"},
        {"ENTER", "Save the tag and open it"},
        {"5", "Read another tag"},
        {"ESC", "Back to NFC menu"},
    }},
    5,
};

constexpr HelpPage kNfcSavedHelp = {
    "TAGS",
    {{
        {"F / X", "Select a tag or folder"},
        {"C / ENTER", "Open it"},
        {"8, then 8", "Delete the selected tag"},
        {"Z / ESC", "Up one folder / back"},
        {"", ""},
    }},
    4,
};

constexpr HelpPage kNfcCardHelp = {
    "TAG",
    {{
        {"ENTER", "Emulate the tag / stop"},
        {"6", "Write it to a tag / stop"},
        {"R / O", "Amiibo: new UID / original"},
        {"8, then 8", "Delete the file"},
        {"Z / ESC", "Stop / back to the tags"},
    }},
    5,
};

constexpr HelpPage kRfidMenuHelp = {
    "125 KHZ",
    {{
        {"F / X", "Move the selection"},
        {"C / ENTER", "Open item"},
        {"ESC", "Back to the main menu"},
        {"", ""},
        {"", ""},
    }},
    3,
};

constexpr HelpPage kRfidSavedHelp = {
    "KEYS",
    {{
        {"F / X", "Select a key or folder"},
        {"C / ENTER", "Open it"},
        {"8, then 8", "Delete the selected key"},
        {"Z / ESC", "Up one folder / back"},
        {"", ""},
    }},
    4,
};

constexpr HelpPage kRfidKeyHelp = {
    "KEY",
    {{
        {"ENTER", "Emulate the key / stop"},
        {"I", "Swap the polarity"},
        {"F / X", "Scroll"},
        {"8, then 8", "Delete the file"},
        {"Z / ESC", "Stop / back to the keys"},
    }},
    5,
};

constexpr HelpPage kRfidAddHelp = {
    "NEW KEY",
    {{
        {"F / X", "Change the digit"},
        {"Z / C", "Move to another digit"},
        {"R", "Random ID"},
        {"ENTER", "Save the key"},
        {"ESC", "Back without saving"},
    }},
    5,
};

constexpr HelpPage kRfidReadHelp = {
    "READ KEY",
    {{
        {"", "Hold an EM4100 tag on the coil"},
        {"ENTER", "Save the key read"},
        {"5", "Read another one"},
        {"F / X", "Scroll"},
        {"ESC", "Back to the menu"},
    }},
    5,
};

constexpr HelpPage kImuMenuHelp = {
    "IMU",
    {{
        {"F / X", "Move the selection"},
        {"C / ENTER", "Open item"},
        {"ESC", "Back to the main menu"},
        {"", ""},
        {"", ""},
    }},
    3,
};

constexpr HelpPage kImuMotionHelp = {
    "MOTION",
    {{
        {"", "BMI270 accel, gyro and tilt; BMM150 field"},
        {"ENTER", "Zero the gyro (hold still)"},
        {"0", "Clear the gyro offset"},
        {"ESC", "Back to the menu"},
        {"", ""},
    }},
    4,
};

constexpr HelpPage kImuCompassHelp = {
    "COMPASS",
    {{
        {"", "Turn the device around every axis"},
        {"", "until calibration reaches 100%"},
        {"ENTER", "Restart the calibration"},
        {"ESC", "Back to the menu"},
        {"", ""},
    }},
    4,
};

constexpr HelpPage kImuLevelHelp = {
    "LEVEL",
    {{
        {"", "The bubble shows the high side"},
        {"ENTER", "Take this tilt as level"},
        {"0", "Clear the level zero"},
        {"ESC", "Back to the menu"},
        {"", ""},
    }},
    4,
};

const HelpPage& help_for(model::AppPage page, bool infrared_info) {
    switch (page) {
        case model::AppPage::SubGhzMenu:
            return kToolMenuHelp;
        case model::AppPage::InfraredMenu:
            return kIrMenuHelp;
        case model::AppPage::IrLearn:
            return kIrLearnHelp;
        case model::AppPage::IrSaved:
            return kIrSavedHelp;
        case model::AppPage::IrRemote:
            return kIrRemoteHelp;
        case model::AppPage::NfcMenu:
            return kNfcMenuHelp;
        case model::AppPage::NfcRead:
            return kNfcReadHelp;
        case model::AppPage::NfcSaved:
            return kNfcSavedHelp;
        case model::AppPage::NfcCard:
            return kNfcCardHelp;
        case model::AppPage::RfidMenu:
            return kRfidMenuHelp;
        case model::AppPage::RfidSaved:
            return kRfidSavedHelp;
        case model::AppPage::RfidKey:
            return kRfidKeyHelp;
        case model::AppPage::RfidAdd:
            return kRfidAddHelp;
        case model::AppPage::RfidRead:
            return kRfidReadHelp;
        case model::AppPage::ImuMenu:
            return kImuMenuHelp;
        case model::AppPage::ImuMotion:
            return kImuMotionHelp;
        case model::AppPage::ImuCompass:
            return kImuCompassHelp;
        case model::AppPage::ImuLevel:
            return kImuLevelHelp;
        case model::AppPage::AddManually:
            return kAddHelp;
        case model::AppPage::Read:
            return kReadHelp;
        case model::AppPage::Saved:
            return kSavedHelp;
        case model::AppPage::Info:
            return infrared_info ? kIrInfoHelp : kInfoHelp;
        case model::AppPage::ReadRaw:
            return kReadRawHelp;
        case model::AppPage::FrequencyAnalyzer:
            return kAnalyzerHelp;
        case model::AppPage::SignalMeter:
            return kMeterHelp;
        case model::AppPage::Menu:
            break;
    }
    return kMenuHelp;
}

} // namespace

HelpPopup::HelpPopup(lv_obj_t* parent,
                     viewmodel::BaseViewModel& view_model,
                     app::AssetManager& assets)
    : BaseWidgets(parent), view_model_(view_model), assets_(assets) {}

void HelpPopup::build() {
    if (core_obj_ || !parent_) {
        return;
    }

    core_obj_ = lv_obj_create(parent_);
    lv_obj_remove_style_all(core_obj_);
    lv_obj_set_size(core_obj_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(core_obj_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(core_obj_, LV_OPA_50, 0);
    lv_obj_add_flag(core_obj_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(core_obj_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(core_obj_, backdrop_clicked_cb, LV_EVENT_CLICKED, this);

    panel_ = lv_obj_create(core_obj_);
    lv_obj_remove_style_all(panel_);
    lv_obj_set_size(panel_, 292, 146);
    lv_obj_center(panel_);
    lv_obj_set_style_radius(panel_, 6, 0);
    lv_obj_set_style_bg_opa(panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel_, 1, 0);
    lv_obj_set_style_shadow_width(panel_, 14, 0);
    lv_obj_set_style_shadow_opa(panel_, LV_OPA_20, 0);
    lv_obj_set_style_pad_all(panel_, 0, 0);
    lv_obj_clear_flag(panel_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel_, LV_OBJ_FLAG_CLICKABLE);

    title_ = lv_label_create(panel_);
    lv_label_set_text(title_, "Keyboard help");
    auto* title_font = assets_.load_standard_font(15);
    lv_obj_set_style_text_font(title_, title_font ? title_font : &lv_font_montserrat_14, 0);
    lv_obj_align(title_, LV_ALIGN_TOP_LEFT, 12, 9);

    page_badge_ = lv_label_create(panel_);
    auto* badge_font = assets_.load_standard_font(11);
    lv_obj_set_style_text_font(page_badge_, badge_font ? badge_font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_radius(page_badge_, 3, 0);
    lv_obj_set_style_pad_hor(page_badge_, 6, 0);
    lv_obj_set_style_pad_ver(page_badge_, 2, 0);
    lv_obj_align(page_badge_, LV_ALIGN_TOP_RIGHT, -10, 7);

    auto* rows = lv_obj_create(panel_);
    lv_obj_remove_style_all(rows);
    lv_obj_set_size(rows, 268, 88);
    lv_obj_align(rows, LV_ALIGN_TOP_MID, 0, 35);
    lv_obj_set_flex_flow(rows, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rows, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(rows, 1, 0);
    lv_obj_clear_flag(rows, LV_OBJ_FLAG_SCROLLABLE);

    auto* key_font = assets_.load_standard_font(11);
    auto* action_font = assets_.load_standard_font(12, app::StandardFontWeight::Regular);
    for (auto& widgets : rows_) {
        widgets.row = lv_obj_create(rows);
        lv_obj_remove_style_all(widgets.row);
        lv_obj_set_size(widgets.row, LV_PCT(100), 16);
        lv_obj_clear_flag(widgets.row, LV_OBJ_FLAG_SCROLLABLE);

        widgets.key = lv_label_create(widgets.row);
        lv_obj_set_width(widgets.key, 82);
        lv_obj_set_style_text_font(widgets.key, key_font ? key_font : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_align(widgets.key, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_align(widgets.key, LV_ALIGN_LEFT_MID, 0, 0);

        widgets.action = lv_label_create(widgets.row);
        lv_obj_set_width(widgets.action, 176);
        lv_obj_set_style_text_font(widgets.action,
                                   action_font ? action_font : &lv_font_montserrat_12,
                                   0);
        lv_obj_align(widgets.action, LV_ALIGN_RIGHT_MID, 0, 0);
    }

    footer_ = lv_label_create(panel_);
    lv_label_set_text(footer_, "HELP / ESC  Close   PRSC  Screenshot");
    lv_label_set_long_mode(footer_, LV_LABEL_LONG_CLIP);
    auto* footer_font = assets_.load_standard_font(10);
    lv_obj_set_style_text_font(footer_, footer_font ? footer_font : &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_align(footer_, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_size(footer_, LV_SIZE_CONTENT, 12);
    lv_obj_align(footer_, LV_ALIGN_BOTTOM_LEFT, 12, -7);

    lv_subject_add_observer_obj(view_model_.dark_mode_subject(),
                                theme_observer_cb,
                                core_obj_,
                                this);
    apply_theme();
    hide();
}

void HelpPopup::show(model::AppPage page) {
    if (!core_obj_) {
        build();
    }
    if (!core_obj_) {
        return;
    }
    update_rows(page);
    apply_theme();
    lv_obj_move_foreground(core_obj_);
    lv_obj_remove_flag(core_obj_, LV_OBJ_FLAG_HIDDEN);
}

void HelpPopup::hide() {
    if (core_obj_ && lv_obj_is_valid(core_obj_)) {
        lv_obj_add_flag(core_obj_, LV_OBJ_FLAG_HIDDEN);
    }
}

bool HelpPopup::visible() const {
    return core_obj_ && lv_obj_is_valid(core_obj_) &&
           !lv_obj_has_flag(core_obj_, LV_OBJ_FLAG_HIDDEN);
}

void HelpPopup::update_rows(model::AppPage page) {
    const auto source = view_model_.info_source();
    const bool infrared_info = source == viewmodel::BaseViewModel::InfoSource::IrLearned ||
                               source == viewmodel::BaseViewModel::InfoSource::IrButton;
    const auto& help = help_for(page, infrared_info);
    lv_label_set_text(page_badge_, help.badge);
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (i >= help.row_count) {
            lv_obj_add_flag(rows_[i].row, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(rows_[i].row, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(rows_[i].key, help.rows[i].key);
        lv_label_set_text(rows_[i].action, help.rows[i].action);
    }
}

void HelpPopup::apply_theme() {
    if (!panel_) {
        return;
    }
    const auto colors = view::palette(view_model_.is_dark_mode());
    lv_obj_set_style_bg_color(panel_, colors.surface, 0);
    lv_obj_set_style_border_color(panel_, colors.border, 0);
    lv_obj_set_style_shadow_color(panel_, lv_color_black(), 0);
    lv_obj_set_style_text_color(title_, colors.text, 0);
    lv_obj_set_style_bg_color(page_badge_, colors.primary, 0);
    lv_obj_set_style_bg_opa(page_badge_, LV_OPA_20, 0);
    lv_obj_set_style_text_color(page_badge_, colors.primary, 0);
    lv_obj_set_style_text_color(footer_, colors.text_disabled, 0);
    for (auto& row : rows_) {
        lv_obj_set_style_text_color(row.key, colors.primary, 0);
        lv_obj_set_style_text_color(row.action, colors.text, 0);
    }
}

void HelpPopup::backdrop_clicked_cb(lv_event_t* event) {
    auto* popup = static_cast<HelpPopup*>(lv_event_get_user_data(event));
    if (popup && lv_event_get_target(event) == popup->core_obj_) {
        popup->hide();
    }
}

void HelpPopup::theme_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* popup = static_cast<HelpPopup*>(lv_observer_get_user_data(observer));
    if (popup) {
        popup->apply_theme();
    }
}

} // namespace view::widgets
