/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace model {

enum class AppPage {
    Menu              = 0,
    FrequencyAnalyzer = 1,
    SignalMeter       = 2,
    ReadRaw           = 3,
    Read              = 4,
    Saved             = 5,
    /* Summary of one signal: a decoded key, a recording, or a saved file. */
    Info              = 6,
    /* Creates a new fixed-code remote. */
    AddManually       = 7,
    /* The Sub-GHz tool's own menu. Menu (0) is the main menu, one row per tool. */
    SubGhzMenu        = 8,
    /* The Infrared tool's menu and pages. */
    InfraredMenu      = 9,
    /* Listens for remotes and lists the signals it receives. */
    IrLearn           = 10,
    /* Browses the remote library: folders and .ir files. */
    IrSaved           = 11,
    /* The buttons of one .ir file. */
    IrRemote          = 12,
    /* The NFC tool's menu and pages. */
    NfcMenu           = 13,
    /* Waits for a tag and reads it. */
    NfcRead           = 14,
    /* Browses the tag library: folders and .nfc files. */
    NfcSaved          = 15,
    /* One saved tag: emulate it, or write it to another tag. */
    NfcCard           = 16,
    /* The 125 kHz RFID tool's menu and pages. */
    RfidMenu          = 17,
    /* Browses the key library: folders and .rfid files. */
    RfidSaved         = 18,
    /* One saved key: emulate it. */
    RfidKey           = 19,
    /* Creates a new EM4100 key. */
    RfidAdd           = 20,
    /* Reads an EM4100 tag with the front-end in reader mode. */
    RfidRead          = 21,
    /* The IMU tool's menu and pages: the BMI270 and the BMM150. */
    ImuMenu           = 22,
    /* Live accelerometer, gyroscope and temperature from the BMI270. */
    ImuMotion         = 23,
    /* Live field from the BMM150, with a compass heading. */
    ImuCompass        = 24,
    /* A bubble level from the accelerometer. */
    ImuLevel          = 25,
};

/* Where a Read RAW session stands. */
enum class RawState {
    Idle      = 0,
    Recording = 1,
    Stopped   = 2,
};

/*
 * Main menu rows, in display order: one per tool, then settings. A new tool
 * gets a row here and a menu page of its own.
 */
enum class MainMenuItem {
    SubGhz = 0,
    Infrared,
    Nfc,
    Imu,
    DarkMode,
};

constexpr std::size_t kMainMenuItemCount = 5;

/* Rows of the Sub-GHz menu, in display order. */
enum class SubGhzMenuItem {
    FrequencyAnalyzer = 0,
    SignalMeter,
    Read,
    ReadRaw,
    Saved,
    AddManually,
};

constexpr std::size_t kSubGhzMenuItemCount = 6;

/* Rows of the Infrared menu, in display order. */
enum class InfraredMenuItem {
    Learn = 0,
    Saved,
};

constexpr std::size_t kInfraredMenuItemCount = 2;

/* Rows of the NFC menu, in display order. */
enum class NfcMenuItem {
    Read = 0,
    Saved,
};

constexpr std::size_t kNfcMenuItemCount = 2;

/* Rows of the 125 kHz RFID menu, in display order. */
enum class RfidMenuItem {
    Read = 0,
    Saved,
    AddManually,
};

constexpr std::size_t kRfidMenuItemCount = 3;

/* Rows of the IMU menu, in display order. */
enum class ImuMenuItem {
    Motion = 0,
    Compass,
    Level,
};

constexpr std::size_t kImuMenuItemCount = 3;
constexpr std::size_t kFrequencyCount = 17;
/* Must match the number of receive presets the radio driver offers. */
constexpr std::size_t kPresetCount = 4;

class BaseModel {
public:
    const char* page_title() const;

    bool dark_mode() const;
    void set_dark_mode(bool enabled);
    void toggle_dark_mode();

    AppPage current_page() const;
    void set_current_page(AppPage page);

    /*
     * Menus. Each menu page remembers its own selection; the three functions
     * below act on the menu that is the current page.
     */
    std::size_t menu_item_count() const;
    std::size_t menu_selection() const;
    void step_menu_selection(bool forward);
    MainMenuItem selected_main_menu_item() const;
    SubGhzMenuItem selected_subghz_menu_item() const;
    InfraredMenuItem selected_infrared_menu_item() const;
    NfcMenuItem selected_nfc_menu_item() const;
    RfidMenuItem selected_rfid_menu_item() const;
    ImuMenuItem selected_imu_menu_item() const;

    /* Receive carrier, chosen from a fixed list of common Sub-GHz channels. */
    std::uint32_t frequency_hz() const;
    static std::uint32_t frequency_hz_at(std::size_t index);
    void select_next_frequency();
    void select_previous_frequency();

    std::size_t preset_index() const;
    void select_next_preset();
    void select_previous_preset();

    /*
     * Read RAW only keeps pulses while the signal is at least this strong.
     * Returns false when the threshold is off and everything is recorded.
     */
    bool raw_threshold_dbm(int& threshold_dbm) const;
    void select_next_raw_threshold();

private:
    bool dark_mode_ = false;
    AppPage current_page_ = AppPage::Menu;
    std::size_t main_menu_index_ = 0;
    std::size_t subghz_menu_index_ = 0;
    std::size_t infrared_menu_index_ = 0;
    std::size_t nfc_menu_index_ = 0;
    std::size_t rfid_menu_index_ = 0;
    std::size_t imu_menu_index_ = 0;
    std::size_t frequency_index_ = 10;
    std::size_t preset_index_ = 1;
    std::size_t raw_threshold_index_ = 4;
};

} // namespace model
