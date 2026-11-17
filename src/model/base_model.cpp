/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "base_model.h"

#include <array>

namespace model {
namespace {

/* The default frequency list of the Flipper Zero Sub-GHz app. Index 10 is 433.92 MHz. */
constexpr std::array<std::uint32_t, kFrequencyCount> kFrequenciesHz = {
    300000000U,
    303875000U,
    304250000U,
    310000000U,
    315000000U,
    318000000U,
    390000000U,
    418000000U,
    433075000U,
    433420000U,
    433920000U,
    434420000U,
    434775000U,
    438900000U,
    868350000U,
    915000000U,
    925000000U,
};

/* Read RAW threshold choices in dBm; 0 stands for "off". Index 4 is -70 dBm. */
constexpr std::array<int, 11> kRawThresholdsDbm = {0, -85, -80, -75, -70, -65, -60, -55, -50, -45, -40};

} // namespace

const char* BaseModel::page_title() const {
    switch (current_page_) {
        case AppPage::FrequencyAnalyzer:
            return "Freq Analyzer";
        case AppPage::SignalMeter:
            return "Signal Meter";
        case AppPage::ReadRaw:
            return "Read RAW";
        case AppPage::Read:
            return "Read";
        case AppPage::Saved:
            return "Saved";
        case AppPage::Info:
            return "Signal Info";
        case AppPage::AddManually:
            return "Add Manually";
        case AppPage::SubGhzMenu:
            return "Sub-GHz";
        case AppPage::InfraredMenu:
            return "Infrared";
        case AppPage::IrLearn:
            return "Learn Remote";
        case AppPage::IrSaved:
            return "Saved Remotes";
        case AppPage::IrRemote:
            return "Remote";
        case AppPage::NfcMenu:
            return "NFC";
        case AppPage::NfcRead:
            return "Read Tag";
        case AppPage::NfcSaved:
            return "Saved Tags";
        case AppPage::NfcCard:
            return "Tag";
        case AppPage::RfidMenu:
            return "125 kHz RFID";
        case AppPage::RfidSaved:
            return "Saved Keys";
        case AppPage::RfidKey:
            return "Key";
        case AppPage::RfidAdd:
            return "Add Manually";
        case AppPage::RfidRead:
            return "Read Key";
        case AppPage::ImuMenu:
            return "IMU";
        case AppPage::ImuMotion:
            return "Motion";
        case AppPage::ImuCompass:
            return "Compass";
        case AppPage::ImuLevel:
            return "Level";
        case AppPage::WebFiles:
            return "Web Files";
        case AppPage::Menu:
            break;
    }
    return "ZeroSub";
}

bool BaseModel::dark_mode() const {
    return dark_mode_;
}

void BaseModel::set_dark_mode(bool enabled) {
    dark_mode_ = enabled;
}

void BaseModel::toggle_dark_mode() {
    dark_mode_ = !dark_mode_;
}

AppPage BaseModel::current_page() const {
    return current_page_;
}

void BaseModel::set_current_page(AppPage page) {
    current_page_ = page;
}

std::size_t BaseModel::menu_item_count() const {
    switch (current_page_) {
        case AppPage::Menu:
            return kMainMenuItemCount;
        case AppPage::SubGhzMenu:
            return kSubGhzMenuItemCount;
        case AppPage::InfraredMenu:
            return kInfraredMenuItemCount;
        case AppPage::NfcMenu:
            return kNfcMenuItemCount;
        case AppPage::RfidMenu:
            return kRfidMenuItemCount;
        case AppPage::ImuMenu:
            return kImuMenuItemCount;
        default:
            return 0;
    }
}

std::size_t BaseModel::menu_selection() const {
    switch (current_page_) {
        case AppPage::SubGhzMenu:
            return subghz_menu_index_;
        case AppPage::InfraredMenu:
            return infrared_menu_index_;
        case AppPage::NfcMenu:
            return nfc_menu_index_;
        case AppPage::RfidMenu:
            return rfid_menu_index_;
        case AppPage::ImuMenu:
            return imu_menu_index_;
        default:
            return main_menu_index_;
    }
}

void BaseModel::step_menu_selection(bool forward) {
    const std::size_t count = menu_item_count();
    if (count == 0) {
        return;
    }
    std::size_t* selection = &main_menu_index_;
    if (current_page_ == AppPage::SubGhzMenu) {
        selection = &subghz_menu_index_;
    }
    else if (current_page_ == AppPage::InfraredMenu) {
        selection = &infrared_menu_index_;
    }
    else if (current_page_ == AppPage::NfcMenu) {
        selection = &nfc_menu_index_;
    }
    else if (current_page_ == AppPage::RfidMenu) {
        selection = &rfid_menu_index_;
    }
    else if (current_page_ == AppPage::ImuMenu) {
        selection = &imu_menu_index_;
    }
    std::size_t& index = *selection;
    index = (index + (forward ? 1 : count - 1)) % count;
}

MainMenuItem BaseModel::selected_main_menu_item() const {
    return static_cast<MainMenuItem>(main_menu_index_);
}

SubGhzMenuItem BaseModel::selected_subghz_menu_item() const {
    return static_cast<SubGhzMenuItem>(subghz_menu_index_);
}

InfraredMenuItem BaseModel::selected_infrared_menu_item() const {
    return static_cast<InfraredMenuItem>(infrared_menu_index_);
}

NfcMenuItem BaseModel::selected_nfc_menu_item() const {
    return static_cast<NfcMenuItem>(nfc_menu_index_);
}

RfidMenuItem BaseModel::selected_rfid_menu_item() const {
    return static_cast<RfidMenuItem>(rfid_menu_index_);
}

ImuMenuItem BaseModel::selected_imu_menu_item() const {
    return static_cast<ImuMenuItem>(imu_menu_index_);
}

std::uint32_t BaseModel::frequency_hz() const {
    return kFrequenciesHz[frequency_index_];
}

std::uint32_t BaseModel::frequency_hz_at(std::size_t index) {
    return kFrequenciesHz[index % kFrequencyCount];
}

void BaseModel::select_next_frequency() {
    frequency_index_ = (frequency_index_ + 1) % kFrequencyCount;
}

void BaseModel::select_previous_frequency() {
    frequency_index_ = (frequency_index_ + kFrequencyCount - 1) % kFrequencyCount;
}

std::size_t BaseModel::preset_index() const {
    return preset_index_;
}

void BaseModel::select_next_preset() {
    preset_index_ = (preset_index_ + 1) % kPresetCount;
}

void BaseModel::select_previous_preset() {
    preset_index_ = (preset_index_ + kPresetCount - 1) % kPresetCount;
}

bool BaseModel::raw_threshold_dbm(int& threshold_dbm) const {
    threshold_dbm = kRawThresholdsDbm[raw_threshold_index_];
    return threshold_dbm != 0;
}

void BaseModel::select_next_raw_threshold() {
    raw_threshold_index_ = (raw_threshold_index_ + 1) % kRawThresholdsDbm.size();
}

} // namespace model
