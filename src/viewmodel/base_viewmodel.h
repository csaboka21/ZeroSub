/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "cap_power.h"
#include "capture_store.h"
#include "cc1101.h"
#include "ir_store.h"
#include "imu_worker.h"
#include "ir_transceiver.h"
#include "nfc_worker.h"
#include "pulse_capture.h"
#include "pulse_transmitter.h"
#include "rfid_emulator.h"
#include "rfid_reader.h"
#include "subjects.h"
#include "web_server.h"
#include "base_model.h"
#include "infrared/ir_protocols.h"
#include "protocols/key_decoder.h"
#include "rfid/em4100.h"

#include "lvgl.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <deque>
#include <string>
#include <vector>

namespace viewmodel {

class BaseViewModel {
public:
    static constexpr std::size_t kAnalyzerChannelCount = model::kFrequencyCount;
    /* Display range for RSSI values, in dBm. Readings are clamped to it. */
    static constexpr int32_t kRssiFloorDbm = -120;
    static constexpr int32_t kRssiCeilingDbm = -20;

    BaseViewModel();
    ~BaseViewModel();

    BaseViewModel(const BaseViewModel&) = delete;
    BaseViewModel& operator=(const BaseViewModel&) = delete;

    lv_subject_t* title_subject();
    lv_subject_t* dark_mode_subject();
    lv_subject_t* current_page_subject();
    lv_subject_t* quit_requested_subject();
    lv_subject_t* menu_selection_subject();

    /* Radio state shared by every radio page. */
    lv_subject_t* radio_status_subject();
    lv_subject_t* radio_ready_subject();

    /* Signal meter. RSSI is in dBm and only meaningful while radio_ready is set. */
    lv_subject_t* radio_frequency_subject();
    lv_subject_t* radio_preset_subject();
    lv_subject_t* radio_rssi_subject();

    /*
     * Read RAW. The state holds a model::RawState. The tick changes on every
     * RSSI reading, so a graph can plot radio_rssi even when the value repeats.
     */
    lv_subject_t* raw_state_subject();
    lv_subject_t* raw_tick_subject();
    lv_subject_t* raw_sample_count_subject();
    lv_subject_t* raw_threshold_subject();
    lv_subject_t* raw_message_subject();
    /* Current recording threshold in dBm; false when it is off. */
    bool raw_threshold_dbm(int32_t& threshold_dbm) const;

    /*
     * Read: decoded keys, newest first. The revision changes whenever the list
     * or a repeat count changes.
     */
    lv_subject_t* read_revision_subject();
    lv_subject_t* read_selection_subject();
    lv_subject_t* read_message_subject();
    std::size_t read_count() const;
    /* One-line description of a decoded key, e.g. "Princeton 24b 95D5D4  x3". */
    std::string read_entry_text(std::size_t index) const;

    /* Saved: the folders and .sub files of one folder in the capture library. */
    lv_subject_t* saved_revision_subject();
    lv_subject_t* saved_selection_subject();
    lv_subject_t* saved_message_subject();
    std::size_t saved_count() const;
    const platform::CaptureFile* saved_entry(std::size_t index) const;

    /*
     * Signal summary: one decoded key, the stopped recording, or a saved file.
     * The revision changes whenever the title or the rows change.
     */
    enum class InfoSource {
        None,
        ReadKey,
        RawRecording,
        SavedFile,
        /* Infrared: a signal on the Learn page, or one button of a saved remote. */
        IrLearned,
        IrButton,
    };
    struct InfoRow {
        std::string label;
        std::string value;
    };
    lv_subject_t* info_revision_subject();
    lv_subject_t* info_message_subject();
    InfoSource info_source() const;
    const std::string& info_title() const;
    std::size_t info_row_count() const;
    const InfoRow* info_row(std::size_t index) const;
    /* False for a rolling code and for an unsaved recording: the page then offers no Send. */
    bool info_can_send() const;

    /*
     * Add Manually: a form that creates a new fixed-code remote. The revision
     * changes whenever a field or the selection changes.
     */
    enum class AddField {
        Protocol = 0,
        Frequency,
        Modulation,
        Key,
    };
    static constexpr int kAddFieldCount = 4;
    lv_subject_t* add_revision_subject();
    lv_subject_t* add_message_subject();
    AddField add_field() const;
    /* Text shown for a field. The key is hex, with the digit being edited in brackets. */
    std::string add_field_value(AddField field) const;

    /*
     * Infrared. Three list pages share these: Learn (the signals received),
     * Saved Remotes (folders and .ir files) and Remote (the buttons of one
     * file). `page` says which list is meant. The revision changes whenever a
     * list or the line under it changes.
     */
    lv_subject_t* ir_revision_subject();
    lv_subject_t* ir_message_subject();
    lv_subject_t* ir_selection_subject(model::AppPage page);
    std::size_t ir_list_count(model::AppPage page) const;
    std::string ir_list_text(model::AppPage page, std::size_t index) const;
    /* The line under the list: receiver status, or a description of the selected row. */
    std::string ir_list_summary(model::AppPage page) const;

    /*
     * NFC. Three pages share these: Read (what is known about the tag just
     * read), Saved Tags (folders and .nfc files) and Tag (one saved tag).
     * `page` says which is meant. On Saved Tags the selected row is a file;
     * on the other two it is only a cursor for scrolling through the lines,
     * which include the tag's NDEF records. The revision changes whenever the
     * rows, that cursor or the line under them change.
     */
    lv_subject_t* nfc_revision_subject();
    lv_subject_t* nfc_message_subject();
    lv_subject_t* nfc_selection_subject();
    std::size_t nfc_list_count(model::AppPage page) const;
    std::string nfc_list_text(model::AppPage page, std::size_t index) const;
    std::size_t nfc_selection(model::AppPage page) const;
    /* The line under the rows: what the reader is doing, or a description of the selected file. */
    std::string nfc_list_summary(model::AppPage page) const;
    /* True while the Tag page is emulating or writing. */
    bool nfc_busy() const;

    /*
     * 125 kHz RFID. Three pages share these: Saved Keys (folders and .rfid
     * files), Key (one saved key, to emulate) and Add Manually (a new EM4100
     * ID). `page` says which. On Saved Keys the selected row is a file; on Key
     * it is only a scrolling cursor; Add Manually shows its fields without
     * one. The revision changes whenever the rows or the line under them
     * change.
     */
    lv_subject_t* rfid_revision_subject();
    lv_subject_t* rfid_message_subject();
    lv_subject_t* rfid_selection_subject();
    std::size_t rfid_list_count(model::AppPage page) const;
    std::string rfid_list_text(model::AppPage page, std::size_t index) const;
    std::size_t rfid_selection(model::AppPage page) const;
    /* The line under the rows: emulation status, or a description of the selected file. */
    std::string rfid_list_summary(model::AppPage page) const;

    /*
     * IMU. Three pages share these: Motion (BMI270 accelerometer, gyroscope
     * and temperature), Compass (BMM150 field and heading) and Level. The
     * revision changes with every new reading.
     */
    lv_subject_t* imu_revision_subject();
    lv_subject_t* imu_message_subject();
    /* Where the chips were found and the latest readings, gyro offset removed. */
    const platform::ImuWorker::Status& imu_status() const;
    bool imu_running() const;
    /* Pitch and roll in degrees from the accelerometer, relative to the zero set on the Level page. */
    bool imu_tilt(float& pitch_deg, float& roll_deg) const;
    /* Field with the hard-iron offset found so far removed, in microtesla. */
    bool imu_field(std::array<float, 3>& field_ut) const;
    /* Heading from the X/Y field, 0-360 degrees; false until calibrated on all sides. */
    bool imu_heading(float& heading_deg) const;
    /* Calibration progress of the compass, 0-100. */
    int32_t imu_calibration_percent() const;

    /*
     * Web Files: an HTTP file manager for the home folder, running while the
     * page is open. The revision changes with the server's status and the
     * network addresses.
     */
    lv_subject_t* web_revision_subject();
    /* Server status; running is false while it is not up. */
    const platform::WebServer::Status& web_status() const;
    /* "http://<ip>:<port>" for every network interface that is up. */
    const std::vector<std::string>& web_urls() const;

    /* Frequency analyzer. The revision changes once per completed sweep. */
    lv_subject_t* analyzer_revision_subject();
    lv_subject_t* analyzer_detected_subject();
    lv_subject_t* analyzer_peak_frequency_subject();
    lv_subject_t* analyzer_peak_rssi_subject();
    lv_subject_t* analyzer_last_subject();
    /* RSSI of one channel from the last completed sweep, in dBm. */
    int32_t analyzer_channel_rssi(std::size_t index) const;
    /* Index of the strongest channel in the last sweep, or kAnalyzerChannelCount if none passed the threshold. */
    std::size_t analyzer_peak_channel() const;

    bool is_dark_mode() const;
    void set_dark_mode(bool enabled);
    void toggle_dark_mode();

    model::AppPage current_page() const;
    void show_page(model::AppPage page);
    /* Shows the menu the current tool page belongs to. */
    void show_menu();
    /*
     * ESC: the summary page returns to the page it was opened from, a tool page
     * to its tool's menu, and a tool's menu to the main menu.
     */
    void go_back();
    void request_quit();

    void menu_next();
    void menu_previous();
    void menu_activate();

    /* The Cap is powered and the radio opened lazily, the first time a radio page is shown. */
    void configure_radio(const platform::Cc1101Config& config);
    void next_frequency();
    void previous_frequency();
    void next_preset();
    void previous_preset();

    /* Directory captures are saved to; empty selects the default. */
    void configure_storage(const std::string& directory);
    /* Starts a recording when idle, stops it when recording. */
    void raw_toggle_record();
    void raw_erase();
    void raw_save();
    void raw_next_threshold();

    void read_next();
    void read_previous();
    /* Saves the selected decoded key as a .sub key file. */
    void read_save_selected();

    void saved_next();
    void saved_previous();
    /* Deletes the selected file. The first call only asks for confirmation; the second one deletes. */
    void saved_delete();
    /* Leaves the current folder. Returns false at the top of the library. */
    bool saved_up();
    bool saved_in_subfolder() const;

    /* Open the summary page for the selected key, the stopped recording, or the selected file.
     * On a folder, saved_show_info() enters it instead. */
    void read_show_info();
    void raw_show_info();
    void saved_show_info();
    /* Saves the key or recording being shown; does nothing for a saved file. */
    void info_save();
    /* Deletes the saved file being shown, with the same two-press confirmation as the Saved page. */
    void info_delete();
    /*
     * Transmits the key or saved file being shown, once. Refused outside the
     * allowed transmit bands (see model/transmit_policy.h). An unsaved recording
     * has to be saved first. Leaving the page stops a transmission.
     */
    void info_send();
    /*
     * Enter on the Saved page: sends the selected file straight away, under the
     * same rules as info_send(), or enters the selected folder.
     */
    void saved_send();

    /*
     * LIRC devices to receive and send on, and the remote library. An empty
     * device picks the first one that can do the job; an empty directory
     * selects the default.
     */
    void configure_infrared(const std::string& receive_device,
                            const std::string& send_device,
                            const std::string& directory);
    /* These act on the infrared list page being shown. */
    void ir_next();
    void ir_previous();
    /* C: summary of the selected signal or button; on Saved Remotes, opens the folder or remote. */
    void ir_open();
    /* Enter: saves the selected signal (Learn), opens the selection (Saved Remotes), or sends the button (Remote). */
    void ir_activate();
    /* Deletes the selected remote or button. The first call only asks for confirmation. */
    void ir_delete();
    /* Learn: saves every listed signal as the buttons of one new remote. */
    void ir_save_all();
    /* Saved Remotes: leaves the current folder. Returns false at the top of the library. */
    bool ir_up();

    /*
     * The ST25R3916 on the Cap and the tag library. An empty SPI device
     * disables the tool; an empty directory selects the default.
     */
    void configure_nfc(const platform::St25r3916Config& config, const std::string& directory);
    /* Saved Tags: move the selection. Read and Tag: scroll the lines. */
    void nfc_next();
    void nfc_previous();
    /* Saved Tags: opens the selected folder or tag. */
    void nfc_open();
    /* Enter: saves the tag just read (Read), opens the selection (Saved Tags), or starts and stops emulating (Tag). */
    void nfc_activate();
    /* Tag: starts and stops writing the saved tag to the tag held against the Cap. */
    void nfc_write();
    /*
     * Tag, amiibo only: the same amiibo with a random new UID, re-signed with
     * the owner's key_retail.bin; restarts the emulation if it runs. The file
     * itself is not changed.
     */
    void nfc_new_uid();
    /* Tag: back to the UID of the file. */
    void nfc_original_uid();
    /* Tag: true when the open file is an amiibo, so a new UID can be made for it. */
    bool nfc_card_is_amiibo() const;
    /* Read: forgets the tag shown and waits for another one. */
    void nfc_read_again();
    /* Deletes the selected or shown tag file. The first call only asks for confirmation. */
    void nfc_delete();
    /* Saved Tags: leaves the current folder. Returns false at the top of the library. */
    bool nfc_up();

    /*
     * The 125 kHz front-end's pins and the key library. A negative modulation
     * GPIO disables the tool; an empty directory selects the default.
     */
    void configure_rfid(const platform::RfidConfig& config, const std::string& directory);
    /* Saved Keys: move the selection. Key: scroll. Add Manually: raise or lower the digit under the cursor. */
    void rfid_next();
    void rfid_previous();
    /* Add Manually: moves the digit cursor. */
    void rfid_cursor(bool right);
    /* Add Manually: a random ID. */
    void rfid_randomize();
    /* Saved Keys: opens the selected folder or key. */
    void rfid_open();
    /* Enter: opens the selection (Saved Keys), starts and stops emulating (Key), or saves (Add Manually). */
    void rfid_activate();
    /* Key: swaps the Manchester polarity, for readers that expect the other one. Not saved in the file. */
    void rfid_invert();
    /* Deletes the selected or shown key file. The first call only asks for confirmation. */
    void rfid_delete();
    /* Saved Keys: leaves the current folder. Returns false at the top of the library. */
    bool rfid_up();
    /* Read Key: forgets the key shown and waits for another one. */
    void rfid_read_again();

    /* The i2c bus of the BMI270; empty searches every bus. */
    void configure_imu(const platform::ImuConfig& config);
    /* Enter: zero the gyroscope (Motion), restart calibration (Compass) or zero the level (Level). */
    void imu_activate();
    /* Back to factory: no gyro offset, no level zero. */
    void imu_clear();

    /* First port the web server tries. */
    void configure_web(uint16_t port);
    /* A new PIN for the web server; every browser has to log in again. */
    void web_new_pin();

    void add_field_next();
    void add_field_previous();
    /* Steps the selected field; on the key it moves the digit cursor instead. */
    void add_value_next();
    void add_value_previous();
    /* Raises or lowers the key digit under the cursor. */
    void add_digit_step(bool up);
    void add_randomize();
    /* Writes the remote as a .sub key file and opens its summary. */
    void add_save();

private:
    enum class RadioMode {
        Off,
        Meter,
        Analyzer,
        Raw,
        Read,
        Transmit,
    };

    /* A transmission that has been asked for and is waiting for the radio or on the air. */
    struct TransmitJob {
        bool pending{false};
        uint32_t frequency_hz{0};
        platform::Cc1101Preset preset{platform::Cc1101Preset::Am650};
        std::vector<int32_t> pulses;
    };

    struct ReadEntry {
        model::DecodedKey key;
        uint32_t frequency_hz;
        platform::Cc1101Preset preset;
        uint32_t repeats;
    };

    struct IrLearnEntry {
        platform::IrSignal signal;
        uint32_t repeats{1};
    };

    void sync_ir_activity();
    void poll_ir();
    void ir_add_capture(const model::IrCapture& capture);
    void ir_refresh_files();
    void ir_describe_selection();
    std::string ir_directory() const;
    std::string ir_browse_directory() const;
    bool ir_in_subfolder() const;
    bool ir_open_remote(const std::string& path);
    void ir_show_info(InfoSource source, const platform::IrSignal& signal, uint32_t repeats);
    /* Sends one signal; progress and the result go to the page that asked. */
    void ir_send(const platform::IrSignal& signal);
    bool ir_delete_button(std::size_t index, std::string& error);
    bool ir_save_learned(const IrLearnEntry& entry, std::string& message);
    void ir_set_message(const std::string& message);
    void ir_disarm_delete();
    void ir_touch();
    static void ir_timer_cb(lv_timer_t* timer);

    void sync_nfc_activity();
    void poll_nfc();
    void nfc_start(platform::NfcWorker::Job job, const model::NfcTag& source);
    void nfc_stop();
    /* Tag page: stops a running job. Returns false when none was running. */
    bool nfc_stop_job();
    void nfc_refresh_files();
    void nfc_describe_selection();
    void nfc_step_selection(int32_t step);
    std::string nfc_directory() const;
    std::string nfc_browse_directory() const;
    bool nfc_in_subfolder() const;
    bool nfc_open_card(const std::string& path);
    void nfc_touch();
    static void nfc_timer_cb(lv_timer_t* timer);

    void sync_rfid_activity();
    void poll_rfid();
    void rfid_start();
    void rfid_stop();
    void rfid_start_reader();
    void poll_rfid_reader();
    /* Key page: stops a running emulation. Returns false when none was running. */
    bool rfid_stop_job();
    void rfid_refresh_files();
    void rfid_describe_selection();
    void rfid_step_selection(int32_t step);
    std::string rfid_directory() const;
    std::string rfid_browse_directory() const;
    bool rfid_in_subfolder() const;
    bool rfid_open_key(const std::string& path);
    void rfid_idle_status();
    void rfid_touch();
    static void rfid_timer_cb(lv_timer_t* timer);

    void sync_imu_activity();
    void poll_imu();
    void imu_set_message();
    void imu_touch();
    static void imu_timer_cb(lv_timer_t* timer);

    void sync_web_activity();
    void poll_web();
    static void web_timer_cb(lv_timer_t* timer);

    void publish_all();
    void publish_meter_settings();
    void step_preset(bool forward);
    RadioMode wanted_radio_mode() const;
    void sync_radio_activity();
    void start_radio();
    void open_radio();
    void stop_radio();
    void begin_radio_mode();
    void retune();
    void poll_raw();
    void begin_transmit();
    void poll_transmit();
    void finish_transmit(const std::string& message);
    /* Builds the pulse train for one .sub file. On refusal `message` says why. */
    bool prepare_file_transmit(const std::string& path, TransmitJob& job, std::string& message);
    /* Checks the band and hands the job to the radio. */
    void start_transmit_job(TransmitJob job);
    /* Shows transmit progress on whichever page started it. */
    void set_transmit_message(const std::string& message);
    void poll_read();
    void add_read_key(const model::DecodedKey& key);
    void refresh_saved();
    void disarm_saved_delete();
    std::string saved_directory() const;
    void describe_saved_selection();
    void select_saved(int32_t index);
    bool save_read_entry(const ReadEntry& entry, std::string& message);
    void open_info(InfoSource source, std::string title, std::vector<InfoRow> rows);
    void add_signal_rows(std::vector<InfoRow>& rows, uint32_t frequency_hz, const std::string& preset) const;
    void add_pulse_rows(std::vector<InfoRow>& rows, const platform::PulseStats& pulses) const;
    /* Opens the summary page for one .sub file. */
    bool show_file_info(const std::string& path, std::string& error);
    void add_prepare();
    void add_publish(const std::string& message);
    void add_step_value(bool forward);
    void add_rolling_rows(std::vector<InfoRow>& rows, const std::string& protocol, uint64_t key) const;
    std::string capture_directory() const;
    void set_raw_state(model::RawState state);
    void publish_raw_threshold();
    bool settings_locked() const;
    void poll_radio();
    void step_analyzer();
    void finish_analyzer_sweep();
    void reset_analyzer();
    void fail_radio(const std::string& error);
    static void radio_timer_cb(lv_timer_t* timer);

    model::BaseModel model_;
    reactive::StringSubject<32> title_subject_;
    reactive::BoolSubject dark_mode_subject_;
    reactive::IntSubject current_page_subject_;
    reactive::BoolSubject quit_requested_subject_;
    reactive::IntSubject menu_selection_subject_;
    reactive::StringSubject<96> radio_status_subject_;
    reactive::BoolSubject radio_ready_subject_;
    reactive::StringSubject<24> radio_frequency_subject_;
    reactive::StringSubject<16> radio_preset_subject_;
    reactive::IntSubject radio_rssi_subject_;
    reactive::IntSubject analyzer_revision_subject_;
    reactive::BoolSubject analyzer_detected_subject_;
    reactive::StringSubject<24> analyzer_peak_frequency_subject_;
    reactive::IntSubject analyzer_peak_rssi_subject_;
    reactive::StringSubject<48> analyzer_last_subject_;
    reactive::IntSubject raw_state_subject_;
    reactive::IntSubject raw_tick_subject_;
    reactive::IntSubject raw_sample_count_subject_;
    reactive::StringSubject<16> raw_threshold_subject_;
    reactive::StringSubject<96> raw_message_subject_;
    reactive::IntSubject read_revision_subject_;
    reactive::IntSubject read_selection_subject_;
    reactive::StringSubject<96> read_message_subject_;
    reactive::IntSubject saved_revision_subject_;
    reactive::IntSubject saved_selection_subject_;
    reactive::StringSubject<96> saved_message_subject_;
    reactive::IntSubject info_revision_subject_;
    reactive::StringSubject<96> info_message_subject_;
    reactive::IntSubject add_revision_subject_;
    reactive::StringSubject<96> add_message_subject_;
    reactive::IntSubject ir_revision_subject_;
    reactive::StringSubject<96> ir_message_subject_;
    reactive::IntSubject ir_learn_selection_subject_;
    reactive::IntSubject ir_saved_selection_subject_;
    reactive::IntSubject ir_remote_selection_subject_;
    reactive::IntSubject nfc_revision_subject_;
    reactive::StringSubject<96> nfc_message_subject_;
    reactive::IntSubject nfc_saved_selection_subject_;
    reactive::IntSubject rfid_revision_subject_;
    reactive::StringSubject<96> rfid_message_subject_;
    reactive::IntSubject rfid_saved_selection_subject_;
    reactive::IntSubject imu_revision_subject_;
    reactive::StringSubject<96> imu_message_subject_;

    /* IMU, as configured. The worker runs while an IMU page or the IMU menu is shown. */
    platform::ImuConfig imu_config_;
    std::unique_ptr<platform::ImuWorker> imu_worker_;
    lv_timer_t* imu_timer_{nullptr};
    uint32_t imu_seen_revision_{0};
    platform::ImuWorker::Status imu_status_;
    /* Gyro reading taken as zero, and the tilt taken as level. */
    std::array<float, 3> imu_gyro_offset_{};
    float imu_pitch_zero_{0.0F};
    float imu_roll_zero_{0.0F};
    /* Compass calibration: the extremes of the field seen per axis. */
    bool imu_field_seen_{false};
    std::array<float, 3> imu_field_min_{};
    std::array<float, 3> imu_field_max_{};

    reactive::IntSubject web_revision_subject_;
    uint16_t web_port_{8080};
    std::unique_ptr<platform::WebServer> web_server_;
    lv_timer_t* web_timer_{nullptr};
    uint32_t web_seen_revision_{0};
    platform::WebServer::Status web_status_;
    std::vector<std::string> web_urls_;

    /* NFC reader and library, as configured. */
    platform::St25r3916Config nfc_config_;
    std::string nfc_directory_;
    /* Exists only while a read, a write or an emulation is running. */
    std::unique_ptr<platform::NfcWorker> nfc_worker_;
    platform::NfcWorker::Job nfc_job_{platform::NfcWorker::Job::Read};
    lv_timer_t* nfc_timer_{nullptr};
    uint32_t nfc_seen_revision_{0};
    /* The line under the rows of the Read and Tag pages. */
    std::string nfc_status_;
    /* Read: the tag read last, until the page is left or another read starts. */
    bool nfc_read_valid_{false};
    model::NfcTag nfc_read_tag_;
    std::vector<std::string> nfc_read_lines_;
    std::size_t nfc_read_line_{0};
    /* Saved Tags: the folder being shown (empty for the library root) and its listing. */
    std::string nfc_path_;
    std::vector<platform::CaptureFile> nfc_files_;
    bool nfc_delete_armed_{false};
    /* Tag: the .nfc file that is open. */
    std::string nfc_card_path_;
    /* What is emulated or written: the file's tag, or the same amiibo with a new UID. */
    model::NfcTag nfc_card_tag_;
    /* The tag as the file holds it. */
    model::NfcTag nfc_card_original_;
    std::vector<std::string> nfc_card_lines_;
    std::size_t nfc_card_line_{0};
    void nfc_set_card_tag(model::NfcTag tag, const std::string& status);
    void nfc_note(const std::string& text);

    /* 125 kHz front-end and key library, as configured. */
    platform::RfidConfig rfid_config_;
    std::string rfid_directory_;
    /* Exists only while the Key page is emulating. */
    std::unique_ptr<platform::RfidEmulator> rfid_emulator_;
    /* Exists only while the Read Key page is open. */
    std::unique_ptr<platform::RfidReader> rfid_reader_;
    uint32_t rfid_reader_seen_revision_{0};
    /* Read Key: the key read last, and the lines describing it. */
    bool rfid_read_valid_{false};
    model::RfidKey rfid_read_key_;
    std::vector<std::string> rfid_read_lines_;
    std::size_t rfid_read_line_{0};
    lv_timer_t* rfid_timer_{nullptr};
    uint32_t rfid_seen_revision_{0};
    /* The line under the rows of the Key and Add Manually pages. */
    std::string rfid_status_;
    /* Saved Keys: the folder being shown (empty for the library root) and its listing. */
    std::string rfid_path_;
    std::vector<platform::CaptureFile> rfid_files_;
    bool rfid_delete_armed_{false};
    /* Key: the .rfid file that is open, its lines and the scroll cursor. */
    std::string rfid_key_path_;
    model::RfidKey rfid_key_;
    std::vector<std::string> rfid_key_lines_;
    std::size_t rfid_key_line_{0};
    /* Manchester polarity used for emulation; resets when another key is opened. */
    bool rfid_inverted_{false};
    /* Add Manually: the ID being edited and the digit under the cursor (0-9). */
    std::array<uint8_t, model::kEm4100DataSize> rfid_add_id_{};
    std::size_t rfid_add_cursor_{0};
    bool rfid_add_initialized_{false};

    /* Infrared devices and library, as configured. */
    std::string ir_receive_device_;
    std::string ir_send_device_;
    std::string ir_directory_;
    /* The receiver exists only while the Learn page is open, the transmitter only while sending. */
    std::unique_ptr<platform::IrReceiver> ir_receiver_;
    std::unique_ptr<platform::IrTransmitter> ir_transmitter_;
    lv_timer_t* ir_timer_{nullptr};
    model::IrCollector ir_collector_;
    uint32_t ir_last_sample_tick_{0};
    std::string ir_status_;
    std::string ir_sending_name_;
    /* Learn: the signals received since the app started, newest first. */
    std::deque<IrLearnEntry> ir_learned_;
    /* Saved Remotes: the folder being shown (empty for the library root) and its listing. */
    std::string ir_path_;
    std::vector<platform::CaptureFile> ir_files_;
    /* Remote: the .ir file that is open and its buttons. */
    std::string ir_remote_path_;
    std::vector<platform::IrSignal> ir_buttons_;
    bool ir_delete_armed_{false};
    /* The signal the summary page shows, and its position in the remote when it is a button. */
    platform::IrSignal info_ir_signal_;
    std::size_t info_ir_index_{0};

    /* Add Manually form. Index 10 of the frequency list is 433.92 MHz, preset 1 is AM650. */
    std::size_t add_protocol_{0};
    std::size_t add_frequency_{10};
    std::size_t add_preset_{1};
    uint64_t add_key_{0};
    AddField add_field_{AddField::Protocol};
    std::size_t add_cursor_{0};
    bool add_initialized_{false};

    /* What the summary page shows. The key is a copy, so the Read list may move on underneath. */
    InfoSource info_source_{InfoSource::None};
    std::string info_title_;
    std::vector<InfoRow> info_rows_;
    ReadEntry info_read_entry_{};
    std::string info_file_path_;
    std::string info_file_name_;
    bool info_delete_armed_{false};
    bool info_sendable_{false};

    platform::Cc1101Config radio_config_;
    platform::CapPower cap_power_;
    /* Poll ticks left before a freshly powered Cap may be probed; 0 when not waiting. */
    uint32_t radio_settle_ticks_{0};
    std::unique_ptr<platform::Cc1101> radio_;
    lv_timer_t* radio_timer_{nullptr};

    /* Read RAW session. The capture exists only while the Read RAW page is open. */
    std::unique_ptr<platform::PulseCapture> capture_;
    /* Exists only while a transmission is on the air. */
    std::unique_ptr<platform::PulseTransmitter> transmitter_;
    TransmitJob transmit_job_;
    model::RawState raw_state_{model::RawState::Idle};
    uint32_t raw_gate_hold_ticks_{0};
    uint32_t raw_frequency_hz_{0};
    platform::Cc1101Preset raw_preset_{platform::Cc1101Preset::Am650};
    std::string capture_directory_;

    /* Read session: decoder state and the keys seen since the app started. */
    model::KeyDecoder decoder_;
    std::deque<ReadEntry> read_history_;

    /* Saved page: the folder being shown (empty for the library root), its listing, and
     * whether a delete is waiting for its second key press. */
    std::string saved_path_;
    std::vector<platform::CaptureFile> saved_files_;
    bool saved_delete_armed_{false};

    /* Channel being measured, the sweep in progress, and the last completed sweep. */
    std::size_t analyzer_index_{0};
    std::array<int32_t, kAnalyzerChannelCount> analyzer_sweep_{};
    std::array<int32_t, kAnalyzerChannelCount> analyzer_channels_{};
    std::size_t analyzer_peak_channel_{kAnalyzerChannelCount};
};

} // namespace viewmodel
