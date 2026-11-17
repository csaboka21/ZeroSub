/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "base_viewmodel.h"

#include "capture_store.h"
#include "transmit_policy.h"

#include <algorithm>
#include <cstdio>

namespace viewmodel {
namespace {

constexpr uint32_t kMeterPollPeriodMs = 150;
/* Dwell per channel while sweeping: long enough for calibration and RSSI to settle. */
constexpr uint32_t kAnalyzerStepPeriodMs = 20;
/* A channel counts as a detected signal at or above this level. Needs tuning on hardware. */
constexpr int32_t kAnalyzerThresholdDbm = -85;
constexpr platform::Cc1101Preset kAnalyzerPreset = platform::Cc1101Preset::Am650;
constexpr const char* kNoFrequencyText = "---.-- MHz";
/* RSSI sampling rate while on the Read RAW page; it also sets how fast the gate reacts. */
constexpr uint32_t kRawPollPeriodMs = 30;
/* Keep recording this long after the signal drops below the threshold. */
constexpr uint32_t kRawGateHoldMs = 150;
/* How often a running transmission is checked for completion. */
constexpr uint32_t kTransmitPollPeriodMs = 50;
/* Recording stops by itself at this many pulses (roughly a 1.5 MB file). */
constexpr std::size_t kRawMaxSamples = 200000;

static_assert(model::kPresetCount == static_cast<std::size_t>(platform::kCc1101PresetCount),
              "the model and the radio driver must agree on the preset count");

int page_to_int(model::AppPage page) {
    return static_cast<int>(page);
}

void format_frequency(char* text, std::size_t size, uint32_t hz) {
    std::snprintf(text,
                  size,
                  "%u.%02u MHz",
                  static_cast<unsigned>(hz / 1000000U),
                  static_cast<unsigned>((hz % 1000000U) / 10000U));
}

uint32_t ticks_for(uint32_t duration_ms, uint32_t period_ms) {
    return (duration_ms + period_ms - 1) / period_ms;
}

} // namespace

BaseViewModel::BaseViewModel()
    : title_subject_(model_.page_title()),
      dark_mode_subject_(model_.dark_mode()),
      current_page_subject_(page_to_int(model_.current_page())),
      quit_requested_subject_(false),
      menu_selection_subject_(static_cast<int32_t>(model_.menu_selection())),
      radio_status_subject_("Radio idle"),
      radio_ready_subject_(false),
      radio_frequency_subject_(""),
      radio_preset_subject_(""),
      radio_rssi_subject_(kRssiFloorDbm),
      analyzer_revision_subject_(0),
      analyzer_detected_subject_(false),
      analyzer_peak_frequency_subject_(kNoFrequencyText),
      analyzer_peak_rssi_subject_(kRssiFloorDbm),
      analyzer_last_subject_(""),
      raw_state_subject_(static_cast<int32_t>(model::RawState::Idle)),
      raw_tick_subject_(0),
      raw_sample_count_subject_(0),
      raw_threshold_subject_(""),
      raw_message_subject_(""),
      read_revision_subject_(0),
      read_selection_subject_(0),
      read_message_subject_(""),
      saved_revision_subject_(0),
      saved_selection_subject_(0),
      saved_message_subject_(""),
      info_revision_subject_(0),
      info_message_subject_(""),
      add_revision_subject_(0),
      add_message_subject_(""),
      ir_revision_subject_(0),
      ir_message_subject_(""),
      ir_learn_selection_subject_(0),
      ir_saved_selection_subject_(0),
      ir_remote_selection_subject_(0),
      nfc_revision_subject_(0),
      nfc_message_subject_(""),
      nfc_saved_selection_subject_(0),
      rfid_revision_subject_(0),
      rfid_message_subject_(""),
      rfid_saved_selection_subject_(0),
      imu_revision_subject_(0),
      imu_message_subject_(""),
      web_revision_subject_(0) {
    publish_raw_threshold();
    analyzer_sweep_.fill(kRssiFloorDbm);
    analyzer_channels_.fill(kRssiFloorDbm);
    publish_meter_settings();
}

BaseViewModel::~BaseViewModel() {
    if (radio_timer_) {
        lv_timer_delete(radio_timer_);
        radio_timer_ = nullptr;
    }
    if (ir_timer_) {
        lv_timer_delete(ir_timer_);
        ir_timer_ = nullptr;
    }
    ir_transmitter_.reset();
    ir_receiver_.reset();
    if (nfc_timer_) {
        lv_timer_delete(nfc_timer_);
        nfc_timer_ = nullptr;
    }
    if (rfid_timer_) {
        lv_timer_delete(rfid_timer_);
        rfid_timer_ = nullptr;
    }
    if (imu_timer_) {
        lv_timer_delete(imu_timer_);
        imu_timer_ = nullptr;
    }
    imu_worker_.reset();
    if (web_timer_) {
        lv_timer_delete(web_timer_);
        web_timer_ = nullptr;
    }
    web_server_.reset();
    /* Ends a running emulation or read, which leaves the coil in tag mode and gives the pins back. */
    rfid_emulator_.reset();
    rfid_reader_.reset();
    /* Ends a running NFC job, which turns the field off. */
    nfc_worker_.reset();
    /* The transmitter and the capture use the radio's data pin, so they go first. */
    transmitter_.reset();
    capture_.reset();
    radio_.reset();
}

lv_subject_t* BaseViewModel::raw_state_subject() {
    return raw_state_subject_.native();
}

lv_subject_t* BaseViewModel::raw_tick_subject() {
    return raw_tick_subject_.native();
}

lv_subject_t* BaseViewModel::raw_sample_count_subject() {
    return raw_sample_count_subject_.native();
}

lv_subject_t* BaseViewModel::raw_threshold_subject() {
    return raw_threshold_subject_.native();
}

lv_subject_t* BaseViewModel::raw_message_subject() {
    return raw_message_subject_.native();
}

bool BaseViewModel::raw_threshold_dbm(int32_t& threshold_dbm) const {
    int value = 0;
    const bool enabled = model_.raw_threshold_dbm(value);
    threshold_dbm = value;
    return enabled;
}

lv_subject_t* BaseViewModel::title_subject() {
    return title_subject_.native();
}

lv_subject_t* BaseViewModel::dark_mode_subject() {
    return dark_mode_subject_.native();
}

lv_subject_t* BaseViewModel::current_page_subject() {
    return current_page_subject_.native();
}

lv_subject_t* BaseViewModel::quit_requested_subject() {
    return quit_requested_subject_.native();
}

lv_subject_t* BaseViewModel::menu_selection_subject() {
    return menu_selection_subject_.native();
}

lv_subject_t* BaseViewModel::radio_status_subject() {
    return radio_status_subject_.native();
}

lv_subject_t* BaseViewModel::radio_ready_subject() {
    return radio_ready_subject_.native();
}

lv_subject_t* BaseViewModel::radio_frequency_subject() {
    return radio_frequency_subject_.native();
}

lv_subject_t* BaseViewModel::radio_preset_subject() {
    return radio_preset_subject_.native();
}

lv_subject_t* BaseViewModel::radio_rssi_subject() {
    return radio_rssi_subject_.native();
}

lv_subject_t* BaseViewModel::analyzer_revision_subject() {
    return analyzer_revision_subject_.native();
}

lv_subject_t* BaseViewModel::analyzer_detected_subject() {
    return analyzer_detected_subject_.native();
}

lv_subject_t* BaseViewModel::analyzer_peak_frequency_subject() {
    return analyzer_peak_frequency_subject_.native();
}

lv_subject_t* BaseViewModel::analyzer_peak_rssi_subject() {
    return analyzer_peak_rssi_subject_.native();
}

lv_subject_t* BaseViewModel::analyzer_last_subject() {
    return analyzer_last_subject_.native();
}

int32_t BaseViewModel::analyzer_channel_rssi(std::size_t index) const {
    return index < analyzer_channels_.size() ? analyzer_channels_[index] : kRssiFloorDbm;
}

std::size_t BaseViewModel::analyzer_peak_channel() const {
    return analyzer_peak_channel_;
}

bool BaseViewModel::is_dark_mode() const {
    return model_.dark_mode();
}

void BaseViewModel::set_dark_mode(bool enabled) {
    model_.set_dark_mode(enabled);
    publish_all();
}

void BaseViewModel::toggle_dark_mode() {
    model_.toggle_dark_mode();
    publish_all();
}

model::AppPage BaseViewModel::current_page() const {
    return model_.current_page();
}

void BaseViewModel::show_page(model::AppPage page) {
    /* Leaving the page ends a transmission; stop_radio() below turns the carrier off. */
    transmit_job_ = TransmitJob{};
    model_.set_current_page(page);
    if (page == model::AppPage::Saved) {
        refresh_saved();
    }
    else if (page == model::AppPage::AddManually) {
        add_prepare();
    }
    else if (page == model::AppPage::IrSaved) {
        ir_refresh_files();
    }
    else if (page == model::AppPage::NfcSaved) {
        nfc_refresh_files();
    }
    else if (page == model::AppPage::RfidSaved) {
        rfid_refresh_files();
    }
    publish_all();
    sync_radio_activity();
    sync_ir_activity();
    sync_nfc_activity();
    sync_rfid_activity();
    sync_imu_activity();
    sync_web_activity();
}

void BaseViewModel::show_menu() {
    /* A tool page goes back to its own tool's menu. */
    switch (model_.current_page()) {
        case model::AppPage::InfraredMenu:
        case model::AppPage::IrLearn:
        case model::AppPage::IrSaved:
        case model::AppPage::IrRemote:
            show_page(model::AppPage::InfraredMenu);
            return;
        case model::AppPage::NfcMenu:
        case model::AppPage::NfcRead:
        case model::AppPage::NfcSaved:
        case model::AppPage::NfcCard:
            show_page(model::AppPage::NfcMenu);
            return;
        case model::AppPage::RfidMenu:
        case model::AppPage::RfidSaved:
        case model::AppPage::RfidKey:
        case model::AppPage::RfidAdd:
        case model::AppPage::RfidRead:
            show_page(model::AppPage::RfidMenu);
            return;
        case model::AppPage::ImuMenu:
        case model::AppPage::ImuMotion:
        case model::AppPage::ImuCompass:
        case model::AppPage::ImuLevel:
            show_page(model::AppPage::ImuMenu);
            return;
        case model::AppPage::Info:
            if (info_source_ == InfoSource::IrLearned || info_source_ == InfoSource::IrButton) {
                show_page(model::AppPage::InfraredMenu);
                return;
            }
            break;
        default:
            break;
    }
    show_page(model::AppPage::SubGhzMenu);
}

void BaseViewModel::go_back() {
    /* While a file is being sent from the Saved page, back only stops the transmission. */
    if (model_.current_page() == model::AppPage::Saved && transmit_job_.pending) {
        transmit_job_ = TransmitJob{};
        stop_radio();
        saved_message_subject_.set("Sending stopped");
        return;
    }
    /* Inside a folder, back means one folder up. */
    if (model_.current_page() == model::AppPage::Saved && saved_up()) {
        return;
    }
    /* Saved Remotes: one folder up; a remote goes back to the list it was opened from. */
    if (model_.current_page() == model::AppPage::IrSaved && ir_up()) {
        return;
    }
    if (model_.current_page() == model::AppPage::IrRemote) {
        show_page(model::AppPage::IrSaved);
        return;
    }
    /* Saved Tags: one folder up. A tag goes back to the list; while it is busy, back only stops it. */
    if (model_.current_page() == model::AppPage::NfcSaved && nfc_up()) {
        return;
    }
    if (model_.current_page() == model::AppPage::NfcCard) {
        if (!nfc_stop_job()) {
            show_page(model::AppPage::NfcSaved);
        }
        return;
    }
    /* Saved Keys: one folder up. A key goes back to the list; while it is emulating, back only stops it. */
    if (model_.current_page() == model::AppPage::RfidSaved && rfid_up()) {
        return;
    }
    if (model_.current_page() == model::AppPage::RfidKey) {
        if (!rfid_stop_job()) {
            show_page(model::AppPage::RfidSaved);
        }
        return;
    }
    /* A tool's menu goes back to the main menu; the main menu has nowhere to go back to. */
    if (model_.current_page() == model::AppPage::SubGhzMenu ||
        model_.current_page() == model::AppPage::InfraredMenu ||
        model_.current_page() == model::AppPage::NfcMenu ||
        model_.current_page() == model::AppPage::RfidMenu ||
        model_.current_page() == model::AppPage::ImuMenu) {
        show_page(model::AppPage::Menu);
        return;
    }
    if (model_.current_page() == model::AppPage::Menu) {
        return;
    }
    /* Web Files hangs off the main menu directly; leaving it stops the server. */
    if (model_.current_page() == model::AppPage::WebFiles) {
        show_page(model::AppPage::Menu);
        return;
    }
    if (model_.current_page() != model::AppPage::Info) {
        show_menu();
        return;
    }

    switch (info_source_) {
        case InfoSource::ReadKey:
            show_page(model::AppPage::Read);
            break;
        case InfoSource::RawRecording:
            show_page(model::AppPage::ReadRaw);
            break;
        case InfoSource::SavedFile:
            show_page(model::AppPage::Saved);
            break;
        case InfoSource::IrLearned:
            show_page(model::AppPage::IrLearn);
            break;
        case InfoSource::IrButton:
            show_page(model::AppPage::IrRemote);
            break;
        case InfoSource::None:
            show_menu();
            break;
    }
}

void BaseViewModel::request_quit() {
    quit_requested_subject_.set(true);
}

void BaseViewModel::menu_next() {
    model_.step_menu_selection(true);
    publish_all();
}

void BaseViewModel::menu_previous() {
    model_.step_menu_selection(false);
    publish_all();
}

void BaseViewModel::menu_activate() {
    if (model_.current_page() == model::AppPage::Menu) {
        switch (model_.selected_main_menu_item()) {
            case model::MainMenuItem::SubGhz:
                show_page(model::AppPage::SubGhzMenu);
                break;
            case model::MainMenuItem::Infrared:
                show_page(model::AppPage::InfraredMenu);
                break;
            case model::MainMenuItem::Nfc:
                show_page(model::AppPage::NfcMenu);
                break;
            case model::MainMenuItem::Imu:
                show_page(model::AppPage::ImuMenu);
                break;
            case model::MainMenuItem::WebFiles:
                show_page(model::AppPage::WebFiles);
                break;
            case model::MainMenuItem::DarkMode:
                toggle_dark_mode();
                break;
        }
        return;
    }
    if (model_.current_page() == model::AppPage::InfraredMenu) {
        switch (model_.selected_infrared_menu_item()) {
            case model::InfraredMenuItem::Learn:
                show_page(model::AppPage::IrLearn);
                break;
            case model::InfraredMenuItem::Saved:
                /* Opening from the menu always starts at the top of the library. */
                ir_path_.clear();
                ir_saved_selection_subject_.set(0);
                show_page(model::AppPage::IrSaved);
                break;
        }
        return;
    }
    if (model_.current_page() == model::AppPage::NfcMenu) {
        switch (model_.selected_nfc_menu_item()) {
            case model::NfcMenuItem::Read:
                show_page(model::AppPage::NfcRead);
                break;
            case model::NfcMenuItem::Saved:
                /* Opening from the menu always starts at the top of the library. */
                nfc_path_.clear();
                nfc_saved_selection_subject_.set(0);
                show_page(model::AppPage::NfcSaved);
                break;
        }
        return;
    }
    if (model_.current_page() == model::AppPage::RfidMenu) {
        switch (model_.selected_rfid_menu_item()) {
            case model::RfidMenuItem::Read:
                show_page(model::AppPage::RfidRead);
                break;
            case model::RfidMenuItem::Saved:
                /* Opening from the menu always starts at the top of the library. */
                rfid_path_.clear();
                rfid_saved_selection_subject_.set(0);
                show_page(model::AppPage::RfidSaved);
                break;
            case model::RfidMenuItem::AddManually:
                show_page(model::AppPage::RfidAdd);
                break;
        }
        return;
    }
    if (model_.current_page() == model::AppPage::ImuMenu) {
        switch (model_.selected_imu_menu_item()) {
            case model::ImuMenuItem::Motion:
                show_page(model::AppPage::ImuMotion);
                break;
            case model::ImuMenuItem::Compass:
                show_page(model::AppPage::ImuCompass);
                break;
            case model::ImuMenuItem::Level:
                show_page(model::AppPage::ImuLevel);
                break;
        }
        return;
    }
    if (model_.current_page() != model::AppPage::SubGhzMenu) {
        return;
    }

    switch (model_.selected_subghz_menu_item()) {
        case model::SubGhzMenuItem::FrequencyAnalyzer:
            show_page(model::AppPage::FrequencyAnalyzer);
            break;
        case model::SubGhzMenuItem::SignalMeter:
            show_page(model::AppPage::SignalMeter);
            break;
        case model::SubGhzMenuItem::ReadRaw:
            show_page(model::AppPage::ReadRaw);
            break;
        case model::SubGhzMenuItem::Read:
            show_page(model::AppPage::Read);
            break;
        case model::SubGhzMenuItem::Saved:
            /* Opening from the menu always starts at the top of the library. */
            saved_path_.clear();
            saved_selection_subject_.set(0);
            show_page(model::AppPage::Saved);
            break;
        case model::SubGhzMenuItem::AddManually:
            show_page(model::AppPage::AddManually);
            break;
    }
}

void BaseViewModel::configure_radio(const platform::Cc1101Config& config) {
    stop_radio();
    radio_.reset();
    radio_config_ = config;
    sync_radio_activity();
}

bool BaseViewModel::settings_locked() const {
    /* A recording must keep the frequency and modulation it was started with. */
    return wanted_radio_mode() == RadioMode::Raw && raw_state_ != model::RawState::Idle;
}

void BaseViewModel::next_frequency() {
    if (settings_locked()) {
        return;
    }
    model_.select_next_frequency();
    publish_meter_settings();
    retune();
}

void BaseViewModel::previous_frequency() {
    if (settings_locked()) {
        return;
    }
    model_.select_previous_frequency();
    publish_meter_settings();
    retune();
}

void BaseViewModel::next_preset() {
    step_preset(true);
}

void BaseViewModel::previous_preset() {
    step_preset(false);
}

void BaseViewModel::step_preset(bool forward) {
    if (settings_locked()) {
        return;
    }
    if (forward) {
        model_.select_next_preset();
    }
    else {
        model_.select_previous_preset();
    }
    publish_meter_settings();
    const RadioMode mode = wanted_radio_mode();
    if (radio_ && (mode == RadioMode::Meter || mode == RadioMode::Raw || mode == RadioMode::Read)) {
        begin_radio_mode();
    }
}

std::string BaseViewModel::capture_directory() const {
    return capture_directory_.empty() ? platform::default_capture_directory() : capture_directory_;
}

void BaseViewModel::configure_storage(const std::string& directory) {
    capture_directory_ = directory;
}

void BaseViewModel::raw_toggle_record() {
    if (wanted_radio_mode() != RadioMode::Raw || !radio_ || !capture_) {
        return;
    }

    if (raw_state_ == model::RawState::Idle) {
        capture_->clear();
        raw_gate_hold_ticks_ = 0;
        raw_frequency_hz_ = model_.frequency_hz();
        raw_preset_ = static_cast<platform::Cc1101Preset>(model_.preset_index());
        raw_sample_count_subject_.set(0);
        raw_message_subject_.set("");
        set_raw_state(model::RawState::Recording);
    }
    else if (raw_state_ == model::RawState::Recording) {
        capture_->set_gate(false);
        raw_sample_count_subject_.set(static_cast<int32_t>(capture_->sample_count()));
        const auto dropped = capture_->dropped_edges();
        if (dropped > 0) {
            char text[96];
            std::snprintf(text,
                          sizeof(text),
                          "%llu edges lost: capture has gaps",
                          static_cast<unsigned long long>(dropped));
            raw_message_subject_.set(text);
        }
        set_raw_state(model::RawState::Stopped);
    }
}

void BaseViewModel::raw_erase() {
    if (raw_state_ != model::RawState::Stopped) {
        return;
    }
    if (capture_) {
        capture_->clear();
    }
    raw_sample_count_subject_.set(0);
    raw_message_subject_.set("");
    set_raw_state(model::RawState::Idle);
}

void BaseViewModel::raw_save() {
    if (raw_state_ != model::RawState::Stopped || !capture_) {
        return;
    }

    platform::RawCapture capture;
    capture.frequency_hz = raw_frequency_hz_;
    capture.preset = platform::cc1101_preset_sub_file_name(raw_preset_);
    capture.samples = capture_->samples();

    std::string path;
    std::string error;
    if (!platform::save_raw_capture(capture_directory(), capture, path, error)) {
        raw_message_subject_.set(("Save failed: " + error).c_str());
        return;
    }

    capture_->clear();
    raw_sample_count_subject_.set(0);
    raw_message_subject_.set(("Saved " + path).c_str());
    set_raw_state(model::RawState::Idle);
}

void BaseViewModel::raw_next_threshold() {
    if (settings_locked()) {
        return;
    }
    model_.select_next_raw_threshold();
    publish_raw_threshold();
}

void BaseViewModel::set_raw_state(model::RawState state) {
    raw_state_ = state;
    raw_state_subject_.set(static_cast<int32_t>(state));
}

void BaseViewModel::publish_raw_threshold() {
    int threshold_dbm = 0;
    if (model_.raw_threshold_dbm(threshold_dbm)) {
        char text[16];
        std::snprintf(text, sizeof(text), "%d dBm", threshold_dbm);
        raw_threshold_subject_.set(text);
    }
    else {
        raw_threshold_subject_.set("Off");
    }
}

void BaseViewModel::publish_all() {
    title_subject_.set(model_.page_title());
    dark_mode_subject_.set(model_.dark_mode());
    menu_selection_subject_.set(static_cast<int32_t>(model_.menu_selection()));
    current_page_subject_.set(page_to_int(model_.current_page()));
}

void BaseViewModel::publish_meter_settings() {
    char text[24];
    format_frequency(text, sizeof(text), model_.frequency_hz());
    radio_frequency_subject_.set(text);
    radio_preset_subject_.set(
        platform::cc1101_preset_name(static_cast<platform::Cc1101Preset>(model_.preset_index())));
}

BaseViewModel::RadioMode BaseViewModel::wanted_radio_mode() const {
    switch (model_.current_page()) {
        case model::AppPage::SignalMeter:
            return RadioMode::Meter;
        case model::AppPage::FrequencyAnalyzer:
            return RadioMode::Analyzer;
        case model::AppPage::ReadRaw:
            return RadioMode::Raw;
        case model::AppPage::Read:
            return RadioMode::Read;
        case model::AppPage::Info:
            if (transmit_job_.pending) {
                return RadioMode::Transmit;
            }
            /* A stopped recording lives in the capture, so its summary keeps the Read RAW session open. */
            return info_source_ == InfoSource::RawRecording ? RadioMode::Raw : RadioMode::Off;
        case model::AppPage::Saved:
            /* A file can be sent straight from the list. */
            if (transmit_job_.pending) {
                return RadioMode::Transmit;
            }
            break;
        case model::AppPage::AddManually:
        case model::AppPage::SubGhzMenu:
        case model::AppPage::Menu:
        /* The infrared pages do not use the radio. */
        case model::AppPage::InfraredMenu:
        case model::AppPage::IrLearn:
        case model::AppPage::IrSaved:
        case model::AppPage::IrRemote:
        /* Nor do the NFC pages: the reader is a chip of its own on the Cap. */
        case model::AppPage::NfcMenu:
        case model::AppPage::NfcRead:
        case model::AppPage::NfcSaved:
        case model::AppPage::NfcCard:
        /* Nor do the 125 kHz pages, which only use two GPIOs. */
        case model::AppPage::RfidMenu:
        case model::AppPage::RfidSaved:
        case model::AppPage::RfidKey:
        case model::AppPage::RfidAdd:
        case model::AppPage::RfidRead:
        /* Nor do the IMU pages, which are on I2C. */
        case model::AppPage::ImuMenu:
        case model::AppPage::ImuMotion:
        case model::AppPage::ImuCompass:
        case model::AppPage::ImuLevel:
        /* Nor does Web Files. */
        case model::AppPage::WebFiles:
            break;
    }
    return RadioMode::Off;
}

void BaseViewModel::sync_radio_activity() {
    if (wanted_radio_mode() == RadioMode::Off) {
        stop_radio();
    }
    else {
        start_radio();
    }
}

void BaseViewModel::start_radio() {
    reset_analyzer();
    radio_ready_subject_.set(false);
    radio_rssi_subject_.set(kRssiFloorDbm);

    if (!radio_) {
        if (radio_config_.spi_device.empty()) {
            radio_status_subject_.set("Set [cc1101] spi_device in zerosub.conf");
            if (transmit_job_.pending) {
                finish_transmit("No radio: set [cc1101] spi_device");
            }
            return;
        }

        if (!cap_power_.enabled()) {
            std::string error;
            if (!cap_power_.enable(error)) {
                fail_radio(error);
                return;
            }
            /* The wait runs on the poll timer so the UI stays responsive. */
            radio_settle_ticks_ = ticks_for(platform::CapPower::kSettleMs, kMeterPollPeriodMs);
        }

        if (radio_settle_ticks_ > 0) {
            radio_status_subject_.set("Powering Cap CC1101...");
        }
        else {
            open_radio();
            if (!radio_) {
                return;
            }
        }
    }
    else {
        begin_radio_mode();
        if (!radio_) {
            return;
        }
    }

    if (!radio_timer_) {
        radio_timer_ = lv_timer_create(radio_timer_cb, kMeterPollPeriodMs, this);
    }
    if (radio_timer_) {
        /* While the Cap settles the wait is counted in meter-period ticks. */
        uint32_t period_ms = kMeterPollPeriodMs;
        if (radio_ && wanted_radio_mode() == RadioMode::Analyzer) {
            period_ms = kAnalyzerStepPeriodMs;
        }
        else if (radio_ && (wanted_radio_mode() == RadioMode::Raw ||
                            wanted_radio_mode() == RadioMode::Read)) {
            period_ms = kRawPollPeriodMs;
        }
        else if (radio_ && wanted_radio_mode() == RadioMode::Transmit) {
            period_ms = kTransmitPollPeriodMs;
        }
        lv_timer_set_period(radio_timer_, period_ms);
        lv_timer_resume(radio_timer_);
    }
}

void BaseViewModel::open_radio() {
    std::string error;
    radio_ = platform::Cc1101::open(radio_config_, error);
    if (!radio_) {
        fail_radio(error);
        return;
    }
    begin_radio_mode();
}

void BaseViewModel::stop_radio() {
    if (radio_timer_) {
        lv_timer_pause(radio_timer_);
    }
    if (transmitter_) {
        /* Take the data line low first, so the carrier is off before the chip is told to idle. */
        transmitter_->stop();
    }
    if (radio_) {
        std::string error;
        /* Stop driving the data pin before the capture gives the pin back. */
        radio_->set_data_output(false, error);
        radio_->idle(error);
        radio_status_subject_.set("Radio idle");
    }
    transmitter_.reset();
    /* Leaving the page discards an unsaved recording. */
    capture_.reset();
    raw_sample_count_subject_.set(0);
    set_raw_state(model::RawState::Idle);
    radio_ready_subject_.set(false);
    radio_rssi_subject_.set(kRssiFloorDbm);
}

void BaseViewModel::begin_radio_mode() {
    const RadioMode mode = wanted_radio_mode();
    if (!radio_ || mode == RadioMode::Off) {
        return;
    }
    if (mode == RadioMode::Transmit) {
        begin_transmit();
        return;
    }

    const bool analyzer = mode == RadioMode::Analyzer;
    const bool read = mode == RadioMode::Read;
    /* Read RAW and Read both take pulses from the radio's data pin. */
    const bool raw = mode == RadioMode::Raw || read;
    const auto preset = analyzer ? kAnalyzerPreset
                                 : static_cast<platform::Cc1101Preset>(model_.preset_index());
    const uint32_t frequency_hz = analyzer ? model::BaseModel::frequency_hz_at(0)
                                           : model_.frequency_hz();

    std::string error;
    if (raw && !capture_) {
        /* The pin becomes a host input before the chip starts driving it. */
        capture_ = platform::PulseCapture::open(radio_config_.gpio_chip,
                                                static_cast<unsigned int>(radio_config_.data_line),
                                                error);
        if (!capture_) {
            fail_radio("Capture: " + error);
            return;
        }
    }
    if (!radio_->set_data_output(raw, error) || !radio_->apply_preset(preset, error) ||
        !radio_->start_receive(frequency_hz, error)) {
        fail_radio(error);
        return;
    }
    if (!raw) {
        capture_.reset();
    }
    if (read) {
        /* Read decodes everything it hears, so the gate stays open. */
        decoder_.reset();
        capture_->clear();
        capture_->set_gate(true);
        read_message_subject_.set("");
    }

    char text[96];
    std::snprintf(text,
                  sizeof(text),
                  "CC1101 v0x%02X %s%s",
                  static_cast<unsigned>(radio_->chip_info().version),
                  analyzer ? "scanning" : (read ? "listening" : "receiving"),
                  radio_->antenna_switch_active() ? "" : " (RF switch not set)");
    radio_status_subject_.set(text);
    radio_ready_subject_.set(true);

    if (analyzer) {
        reset_analyzer();
    }
    if (radio_timer_) {
        lv_timer_set_period(radio_timer_,
                            analyzer ? kAnalyzerStepPeriodMs
                                     : (raw ? kRawPollPeriodMs : kMeterPollPeriodMs));
    }
}

void BaseViewModel::retune() {
    const RadioMode mode = wanted_radio_mode();
    if (!radio_ ||
        (mode != RadioMode::Meter && mode != RadioMode::Raw && mode != RadioMode::Read)) {
        return;
    }

    std::string error;
    if (!radio_->start_receive(model_.frequency_hz(), error)) {
        fail_radio(error);
    }
}

void BaseViewModel::poll_radio() {
    if (!radio_) {
        if (radio_settle_ticks_ > 0 && --radio_settle_ticks_ == 0) {
            open_radio();
        }
        return;
    }

    switch (wanted_radio_mode()) {
        case RadioMode::Meter: {
            int32_t rssi_dbm = kRssiFloorDbm;
            std::string error;
            if (!radio_->read_rssi_dbm(rssi_dbm, error)) {
                fail_radio(error);
                return;
            }
            radio_rssi_subject_.set(std::clamp(rssi_dbm, kRssiFloorDbm, kRssiCeilingDbm));
            break;
        }
        case RadioMode::Analyzer:
            step_analyzer();
            break;
        case RadioMode::Raw:
            poll_raw();
            break;
        case RadioMode::Read:
            poll_read();
            break;
        case RadioMode::Transmit:
            poll_transmit();
            break;
        case RadioMode::Off:
            /* Nothing left to do, for example right after a transmission: release the radio. */
            stop_radio();
            break;
    }
}

void BaseViewModel::begin_transmit() {
    if (transmitter_ || !radio_) {
        return;
    }
    /* The capture and the transmitter cannot both hold the data line. */
    capture_.reset();

    std::string error;
    if (!radio_->set_data_output(false, error) || !radio_->apply_preset(transmit_job_.preset, error)) {
        fail_radio(error);
        return;
    }

    transmitter_ = platform::PulseTransmitter::open(radio_config_.gpio_chip,
                                                    static_cast<unsigned int>(radio_config_.data_line),
                                                    error);
    if (!transmitter_) {
        finish_transmit("Cannot send: " + error);
        return;
    }

    /* The one gate every transmission passes, immediately before the carrier is turned on. */
    std::string reason;
    if (!model::transmit_allowed(transmit_job_.frequency_hz, reason)) {
        finish_transmit(reason);
        return;
    }
    if (!radio_->start_transmit(transmit_job_.frequency_hz, error)) {
        transmitter_.reset();
        fail_radio(error);
        return;
    }

    transmitter_->start(std::move(transmit_job_.pulses));
    transmit_job_.pulses.clear();
    radio_status_subject_.set("CC1101 transmitting");
    set_transmit_message("Sending... ESC stops");
    if (radio_timer_) {
        lv_timer_set_period(radio_timer_, kTransmitPollPeriodMs);
    }
}

void BaseViewModel::poll_transmit() {
    if (!transmitter_ || transmitter_->running()) {
        return;
    }

    const auto result = transmitter_->result();
    if (!result.completed) {
        finish_transmit(result.error.empty() ? "Sending stopped" : "Send failed: " + result.error);
        return;
    }

    char text[96];
    std::snprintf(text,
                  sizeof(text),
                  "Sent on %u.%02u MHz. Worst edge %u us late",
                  static_cast<unsigned>(transmit_job_.frequency_hz / 1000000U),
                  static_cast<unsigned>((transmit_job_.frequency_hz % 1000000U) / 10000U),
                  static_cast<unsigned>(result.worst_late_us));
    finish_transmit(text);
}

void BaseViewModel::finish_transmit(const std::string& message) {
    /* Data line low and released first, then the chip leaves transmit. */
    transmitter_.reset();
    if (radio_) {
        std::string ignored;
        radio_->idle(ignored);
    }
    transmit_job_ = TransmitJob{};
    set_transmit_message(message);
    /* The poll timer sees that nothing is wanted any more and releases the radio. */
}

void BaseViewModel::poll_raw() {
    int32_t rssi_dbm = kRssiFloorDbm;
    std::string error;
    if (!radio_->read_rssi_dbm(rssi_dbm, error)) {
        fail_radio(error);
        return;
    }
    radio_rssi_subject_.set(std::clamp(rssi_dbm, kRssiFloorDbm, kRssiCeilingDbm));
    raw_tick_subject_.set(raw_tick_subject_.value() + 1);

    if (!capture_ || raw_state_ != model::RawState::Recording) {
        return;
    }

    int threshold_dbm = 0;
    const bool strong_enough = !model_.raw_threshold_dbm(threshold_dbm) || rssi_dbm >= threshold_dbm;
    if (strong_enough) {
        raw_gate_hold_ticks_ = ticks_for(kRawGateHoldMs, kRawPollPeriodMs);
    }
    else if (raw_gate_hold_ticks_ > 0) {
        --raw_gate_hold_ticks_;
    }
    capture_->set_gate(strong_enough || raw_gate_hold_ticks_ > 0);

    const std::size_t count = capture_->sample_count();
    raw_sample_count_subject_.set(static_cast<int32_t>(count));
    if (count >= kRawMaxSamples) {
        raw_toggle_record();
        raw_message_subject_.set("Capture full");
    }
}

void BaseViewModel::step_analyzer() {
    /* The radio has been dwelling on analyzer_index_ since the previous tick. */
    int32_t rssi_dbm = kRssiFloorDbm;
    std::string error;
    if (!radio_->read_rssi_dbm(rssi_dbm, error)) {
        fail_radio(error);
        return;
    }
    analyzer_sweep_[analyzer_index_] = std::clamp(rssi_dbm, kRssiFloorDbm, kRssiCeilingDbm);

    if (++analyzer_index_ >= kAnalyzerChannelCount) {
        analyzer_index_ = 0;
        finish_analyzer_sweep();
    }

    if (!radio_->start_receive(model::BaseModel::frequency_hz_at(analyzer_index_), error)) {
        fail_radio(error);
    }
}

void BaseViewModel::finish_analyzer_sweep() {
    analyzer_channels_ = analyzer_sweep_;

    const auto strongest = std::max_element(analyzer_channels_.begin(), analyzer_channels_.end());
    const auto channel = static_cast<std::size_t>(strongest - analyzer_channels_.begin());
    const bool detected = *strongest >= kAnalyzerThresholdDbm;

    if (detected) {
        char frequency[24];
        format_frequency(frequency, sizeof(frequency), model::BaseModel::frequency_hz_at(channel));
        char last[48];
        std::snprintf(last, sizeof(last), "Last: %s  %d dBm", frequency, static_cast<int>(*strongest));

        analyzer_peak_channel_ = channel;
        analyzer_peak_frequency_subject_.set(frequency);
        analyzer_peak_rssi_subject_.set(*strongest);
        analyzer_last_subject_.set(last);
    }
    else {
        analyzer_peak_channel_ = kAnalyzerChannelCount;
        analyzer_peak_frequency_subject_.set(kNoFrequencyText);
        analyzer_peak_rssi_subject_.set(kRssiFloorDbm);
    }
    analyzer_detected_subject_.set(detected);
    analyzer_revision_subject_.set(analyzer_revision_subject_.value() + 1);
}

void BaseViewModel::reset_analyzer() {
    analyzer_index_ = 0;
    analyzer_sweep_.fill(kRssiFloorDbm);
    analyzer_channels_.fill(kRssiFloorDbm);
    analyzer_peak_channel_ = kAnalyzerChannelCount;
    analyzer_peak_frequency_subject_.set(kNoFrequencyText);
    analyzer_peak_rssi_subject_.set(kRssiFloorDbm);
    analyzer_last_subject_.set("");
    analyzer_detected_subject_.set(false);
    analyzer_revision_subject_.set(analyzer_revision_subject_.value() + 1);
}

void BaseViewModel::fail_radio(const std::string& error) {
    if (radio_timer_) {
        lv_timer_pause(radio_timer_);
    }
    if (transmit_job_.pending) {
        transmit_job_ = TransmitJob{};
        set_transmit_message("Cannot send: " + error);
    }
    /* Drop the handles so the next visit to a radio page retries from scratch. */
    transmitter_.reset();
    capture_.reset();
    radio_.reset();
    raw_sample_count_subject_.set(0);
    set_raw_state(model::RawState::Idle);
    radio_settle_ticks_ = 0;
    radio_ready_subject_.set(false);
    radio_rssi_subject_.set(kRssiFloorDbm);
    radio_status_subject_.set(error.empty() ? "Radio error" : error.c_str());
}

void BaseViewModel::radio_timer_cb(lv_timer_t* timer) {
    auto* view_model = static_cast<BaseViewModel*>(lv_timer_get_user_data(timer));
    if (view_model) {
        view_model->poll_radio();
    }
}

} // namespace viewmodel
