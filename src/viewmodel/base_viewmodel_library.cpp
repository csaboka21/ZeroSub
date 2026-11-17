/*
 * SPDX-License-Identifier: MIT
 *
 * BaseViewModel: the Read page (live key decoding) and the Saved page (file list).
 */

#include "base_viewmodel.h"

#include "protocols/key_encoder.h"
#include "protocols/rolling_codes.h"
#include "transmit_policy.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <utility>

namespace viewmodel {
namespace {

/* Decoded keys kept in the Read list. */
constexpr std::size_t kMaxReadEntries = 30;

std::string file_name_of(const std::string& path) {
    return std::filesystem::path(path).filename().string();
}

/* "0x1A2B", or "0x1A2B +5C" when a long frame has bits after the key. */
std::string key_text(uint64_t key, uint64_t extra = 0, uint32_t extra_bits = 0) {
    char text[48];
    if (extra_bits > 0) {
        std::snprintf(text,
                      sizeof(text),
                      "0x%llX +%0*llX",
                      static_cast<unsigned long long>(key),
                      static_cast<int>((extra_bits + 3) / 4),
                      static_cast<unsigned long long>(extra));
    }
    else {
        std::snprintf(text, sizeof(text), "0x%llX", static_cast<unsigned long long>(key));
    }
    return text;
}

/* "SN 1234567, button 2" for a rolling code that shows its remote; empty otherwise. */
std::string rolling_remote_text(const std::string& protocol, uint64_t key) {
    model::RollingIdentity identity;
    if (!model::rolling_identity(protocol, key, identity)) {
        return std::string();
    }
    char text[48];
    std::snprintf(text,
                  sizeof(text),
                  "SN %X, button %X",
                  static_cast<unsigned>(identity.serial),
                  static_cast<unsigned>(identity.button));
    return text;
}

/* Two rolling-code frames from the same remote and button: the same thing pressed again. */
bool same_rolling_remote(const model::DecodedKey& left, const model::DecodedKey& right) {
    const std::string protocol = left.protocol;
    model::RollingIdentity left_identity;
    model::RollingIdentity right_identity;
    return protocol == right.protocol && model::rolling_identity(protocol, left.key, left_identity) &&
           model::rolling_identity(protocol, right.key, right_identity) &&
           left_identity.serial == right_identity.serial && left_identity.button == right_identity.button;
}

/* Longest RAW recording that will be sent: the same limit as a recording made here. */
constexpr std::size_t kMaxTransmitPulses = 200000;

bool preset_from_sub_file_name(const std::string& sub_file_name, platform::Cc1101Preset& preset) {
    for (int index = 0; index < platform::kCc1101PresetCount; ++index) {
        const auto candidate = static_cast<platform::Cc1101Preset>(index);
        if (sub_file_name == platform::cc1101_preset_sub_file_name(candidate)) {
            preset = candidate;
            return true;
        }
    }
    return false;
}

/* "AM650" for a preset this app knows; otherwise the file's own name without the Flipper prefix. */
std::string preset_display_name(const std::string& sub_file_name) {
    for (int index = 0; index < platform::kCc1101PresetCount; ++index) {
        const auto preset = static_cast<platform::Cc1101Preset>(index);
        if (sub_file_name == platform::cc1101_preset_sub_file_name(preset)) {
            return platform::cc1101_preset_name(preset);
        }
    }
    const std::string prefix = "FuriHalSubGhzPreset";
    if (sub_file_name.compare(0, prefix.size(), prefix) == 0) {
        return sub_file_name.substr(prefix.size());
    }
    return sub_file_name.empty() ? "Unknown" : sub_file_name;
}

} // namespace

lv_subject_t* BaseViewModel::read_revision_subject() {
    return read_revision_subject_.native();
}

lv_subject_t* BaseViewModel::read_selection_subject() {
    return read_selection_subject_.native();
}

lv_subject_t* BaseViewModel::read_message_subject() {
    return read_message_subject_.native();
}

std::size_t BaseViewModel::read_count() const {
    return read_history_.size();
}

std::string BaseViewModel::read_entry_text(std::size_t index) const {
    if (index >= read_history_.size()) {
        return std::string();
    }

    const auto& entry = read_history_[index];
    char text[96];
    if (model::is_rolling_protocol(entry.key.protocol)) {
        /* Rolling codes are marked, and shown by remote where the frame tells which one it is. */
        const std::string remote = rolling_remote_text(entry.key.protocol, entry.key.key);
        char code[24];
        std::snprintf(code, sizeof(code), "%llX", static_cast<unsigned long long>(entry.key.key));
        std::snprintf(text,
                      sizeof(text),
                      "[R] %s %s  %u.%02u  x%u",
                      entry.key.protocol,
                      remote.empty() ? code : remote.c_str(),
                      static_cast<unsigned>(entry.frequency_hz / 1000000U),
                      static_cast<unsigned>((entry.frequency_hz % 1000000U) / 10000U),
                      static_cast<unsigned>(entry.repeats));
        return text;
    }
    std::snprintf(text,
                  sizeof(text),
                  "%s %ub %llX  %u.%02u  x%u",
                  entry.key.protocol,
                  static_cast<unsigned>(entry.key.bits),
                  static_cast<unsigned long long>(entry.key.key),
                  static_cast<unsigned>(entry.frequency_hz / 1000000U),
                  static_cast<unsigned>((entry.frequency_hz % 1000000U) / 10000U),
                  static_cast<unsigned>(entry.repeats));
    return text;
}

void BaseViewModel::poll_read() {
    int32_t rssi_dbm = kRssiFloorDbm;
    std::string error;
    if (!radio_->read_rssi_dbm(rssi_dbm, error)) {
        fail_radio(error);
        return;
    }
    radio_rssi_subject_.set(std::clamp(rssi_dbm, kRssiFloorDbm, kRssiCeilingDbm));

    if (!capture_) {
        return;
    }
    bool changed = false;
    model::DecodedKey key;
    for (const int32_t duration_us : capture_->drain()) {
        if (decoder_.feed(duration_us, key)) {
            add_read_key(key);
            changed = true;
        }
    }
    if (changed) {
        read_revision_subject_.set(read_revision_subject_.value() + 1);
    }
}

void BaseViewModel::add_read_key(const model::DecodedKey& key) {
    const uint32_t frequency_hz = model_.frequency_hz();

    /* A held button repeats its frame: count it on the newest entry instead of adding rows. */
    if (!read_history_.empty() && read_history_.front().key.same_key(key) &&
        read_history_.front().frequency_hz == frequency_hz) {
        ++read_history_.front().repeats;
        read_history_.front().key.te_us = key.te_us;
        return;
    }
    /* The next press of a rolling-code button is a new code: keep one row, showing the latest. */
    if (!read_history_.empty() && read_history_.front().frequency_hz == frequency_hz &&
        same_rolling_remote(read_history_.front().key, key)) {
        ++read_history_.front().repeats;
        read_history_.front().key = key;
        return;
    }

    read_history_.push_front(
        {key, frequency_hz, static_cast<platform::Cc1101Preset>(model_.preset_index()), 1});
    if (read_history_.size() > kMaxReadEntries) {
        read_history_.pop_back();
    }

    /* Keep the cursor on the same key, unless it was on the newest one. */
    const int32_t selection = read_selection_subject_.value();
    if (selection > 0) {
        read_selection_subject_.set(
            std::min<int32_t>(selection + 1, static_cast<int32_t>(read_history_.size()) - 1));
    }
}

void BaseViewModel::read_next() {
    const auto count = static_cast<int32_t>(read_history_.size());
    if (count > 0) {
        read_selection_subject_.set(std::min(read_selection_subject_.value() + 1, count - 1));
    }
}

void BaseViewModel::read_previous() {
    read_selection_subject_.set(std::max<int32_t>(read_selection_subject_.value() - 1, 0));
}

void BaseViewModel::read_save_selected() {
    const auto index = static_cast<std::size_t>(read_selection_subject_.value());
    if (index >= read_history_.size()) {
        return;
    }

    std::string message;
    save_read_entry(read_history_[index], message);
    read_message_subject_.set(message.c_str());
}

bool BaseViewModel::save_read_entry(const ReadEntry& entry, std::string& message) {
    platform::KeyCapture capture;
    capture.frequency_hz = entry.frequency_hz;
    capture.preset = platform::cc1101_preset_sub_file_name(entry.preset);
    capture.protocol = entry.key.protocol;
    capture.bits = entry.key.bits;
    capture.key = entry.key.key;
    capture.te_us = entry.key.te_us;
    capture.extra = entry.key.extra;
    capture.extra_bits = entry.key.extra_bits;

    std::string path;
    std::string error;
    if (platform::save_key_capture(capture_directory(), capture, path, error)) {
        message = "Saved " + file_name_of(path);
        return true;
    }
    message = "Save failed: " + error;
    return false;
}

lv_subject_t* BaseViewModel::saved_revision_subject() {
    return saved_revision_subject_.native();
}

lv_subject_t* BaseViewModel::saved_selection_subject() {
    return saved_selection_subject_.native();
}

lv_subject_t* BaseViewModel::saved_message_subject() {
    return saved_message_subject_.native();
}

std::size_t BaseViewModel::saved_count() const {
    return saved_files_.size();
}

const platform::CaptureFile* BaseViewModel::saved_entry(std::size_t index) const {
    return index < saved_files_.size() ? &saved_files_[index] : nullptr;
}

bool BaseViewModel::saved_in_subfolder() const {
    return !saved_path_.empty() && saved_path_ != capture_directory();
}

std::string BaseViewModel::saved_directory() const {
    return saved_in_subfolder() ? saved_path_ : capture_directory();
}

void BaseViewModel::refresh_saved() {
    saved_delete_armed_ = false;

    /* A folder outside the library (the card was swapped, the config changed) falls back to its root. */
    const std::string root = capture_directory();
    if (saved_path_.compare(0, root.size(), root) != 0) {
        saved_path_.clear();
    }

    std::string error;
    /* Make sure the library exists, so there is a folder to copy files into before the first save. */
    platform::ensure_capture_directory(root, error);
    if (platform::list_directory(saved_directory(), saved_files_, error)) {
        saved_message_subject_.set(saved_directory().c_str());
    }
    else {
        saved_message_subject_.set(error.c_str());
    }

    const auto last = saved_files_.empty() ? 0 : static_cast<int32_t>(saved_files_.size()) - 1;
    saved_selection_subject_.set(std::clamp<int32_t>(saved_selection_subject_.value(), 0, last));
    describe_saved_selection();
    saved_revision_subject_.set(saved_revision_subject_.value() + 1);
}

void BaseViewModel::describe_saved_selection() {
    /* File headers are only read for the selected row, so large folders list quickly. */
    const auto index = static_cast<std::size_t>(saved_selection_subject_.value());
    if (index < saved_files_.size() && saved_files_[index].summary.empty()) {
        auto& entry = saved_files_[index];
        entry.summary = entry.is_directory ? "Folder" : platform::describe_capture(entry.path);
    }
}

void BaseViewModel::select_saved(int32_t index) {
    saved_selection_subject_.set(index);
    describe_saved_selection();
    /* The summary line is refreshed even when the index itself did not change. */
    saved_revision_subject_.set(saved_revision_subject_.value() + 1);
}

void BaseViewModel::disarm_saved_delete() {
    if (saved_delete_armed_) {
        saved_delete_armed_ = false;
        saved_message_subject_.set(saved_directory().c_str());
    }
}

void BaseViewModel::saved_next() {
    disarm_saved_delete();
    const auto count = static_cast<int32_t>(saved_files_.size());
    if (count > 0) {
        select_saved(std::min(saved_selection_subject_.value() + 1, count - 1));
    }
}

void BaseViewModel::saved_previous() {
    disarm_saved_delete();
    select_saved(std::max<int32_t>(saved_selection_subject_.value() - 1, 0));
}

bool BaseViewModel::saved_up() {
    if (!saved_in_subfolder()) {
        return false;
    }

    const std::filesystem::path current(saved_path_);
    const std::string left_folder = current.filename().string();
    saved_path_ = current.parent_path().string();
    saved_selection_subject_.set(0);
    refresh_saved();

    /* Put the cursor back on the folder that was just left. */
    for (std::size_t index = 0; index < saved_files_.size(); ++index) {
        if (saved_files_[index].is_directory && saved_files_[index].name == left_folder) {
            select_saved(static_cast<int32_t>(index));
            break;
        }
    }
    return true;
}

void BaseViewModel::saved_delete() {
    const auto index = static_cast<std::size_t>(saved_selection_subject_.value());
    if (index >= saved_files_.size()) {
        return;
    }
    if (saved_files_[index].is_directory) {
        saved_message_subject_.set("Folders cannot be deleted here");
        return;
    }

    if (!saved_delete_armed_) {
        saved_delete_armed_ = true;
        saved_message_subject_.set(("Press 8 again to delete " + saved_files_[index].name).c_str());
        return;
    }

    const std::string name = saved_files_[index].name;
    std::string error;
    const bool deleted = platform::delete_capture(saved_files_[index].path, error);
    refresh_saved();
    saved_message_subject_.set(deleted ? ("Deleted " + name).c_str()
                                       : ("Delete failed: " + error).c_str());
}

lv_subject_t* BaseViewModel::info_revision_subject() {
    return info_revision_subject_.native();
}

lv_subject_t* BaseViewModel::info_message_subject() {
    return info_message_subject_.native();
}

BaseViewModel::InfoSource BaseViewModel::info_source() const {
    return info_source_;
}

const std::string& BaseViewModel::info_title() const {
    return info_title_;
}

std::size_t BaseViewModel::info_row_count() const {
    return info_rows_.size();
}

const BaseViewModel::InfoRow* BaseViewModel::info_row(std::size_t index) const {
    return index < info_rows_.size() ? &info_rows_[index] : nullptr;
}

void BaseViewModel::open_info(InfoSource source, std::string title, std::vector<InfoRow> rows) {
    info_source_ = source;
    info_title_ = std::move(title);
    info_rows_ = std::move(rows);
    info_delete_armed_ = false;
    info_revision_subject_.set(info_revision_subject_.value() + 1);
    show_page(model::AppPage::Info);
}

void BaseViewModel::add_signal_rows(std::vector<InfoRow>& rows,
                                    uint32_t frequency_hz,
                                    const std::string& preset) const {
    char text[32];
    std::snprintf(text,
                  sizeof(text),
                  "%u.%02u MHz",
                  static_cast<unsigned>(frequency_hz / 1000000U),
                  static_cast<unsigned>((frequency_hz % 1000000U) / 10000U));
    rows.push_back({"Frequency", text});
    rows.push_back({"Modulation", preset_display_name(preset)});
}

void BaseViewModel::add_rolling_rows(std::vector<InfoRow>& rows,
                                     const std::string& protocol,
                                     uint64_t key) const {
    rows.push_back({"Type", "Rolling code"});
    const std::string remote = rolling_remote_text(protocol, key);
    if (!remote.empty()) {
        rows.push_back({"Remote", remote});
    }
}

bool BaseViewModel::info_can_send() const {
    return info_sendable_;
}

void BaseViewModel::add_pulse_rows(std::vector<InfoRow>& rows,
                                   const platform::PulseStats& pulses) const {
    char text[32];
    std::snprintf(text, sizeof(text), "%llu", static_cast<unsigned long long>(pulses.count));
    rows.push_back({"Pulses", text});

    const unsigned long long total_ms = pulses.duration_us / 1000ULL;
    if (total_ms >= 1000ULL) {
        std::snprintf(text, sizeof(text), "%llu.%02llu s", total_ms / 1000ULL, (total_ms % 1000ULL) / 10ULL);
    }
    else {
        std::snprintf(text, sizeof(text), "%llu ms", total_ms);
    }
    rows.push_back({"Length", text});

    std::snprintf(text, sizeof(text), "%u us", static_cast<unsigned>(pulses.shortest_us));
    rows.push_back({"Shortest pulse", text});
}

void BaseViewModel::read_show_info() {
    const auto index = static_cast<std::size_t>(read_selection_subject_.value());
    if (index >= read_history_.size()) {
        return;
    }
    info_read_entry_ = read_history_[index];
    const auto& entry = info_read_entry_;

    std::vector<InfoRow> rows;
    rows.push_back({"Key", key_text(entry.key.key, entry.key.extra, entry.key.extra_bits)});
    info_sendable_ = !model::is_rolling_protocol(entry.key.protocol);
    if (info_sendable_) {
        add_signal_rows(rows, entry.frequency_hz, platform::cc1101_preset_sub_file_name(entry.preset));
        rows.push_back({"Pulse length", std::to_string(entry.key.te_us) + " us"});
        rows.push_back({"Seen", std::to_string(entry.repeats) + (entry.repeats == 1 ? " time" : " times")});
        info_message_subject_.set("ENTER sends this key, 5 saves it.");
    }
    else {
        add_rolling_rows(rows, entry.key.protocol, entry.key.key);
        add_signal_rows(rows, entry.frequency_hz, platform::cc1101_preset_sub_file_name(entry.preset));
        info_message_subject_.set("Rolling code: 5 saves it for reference. It cannot be sent.");
    }
    open_info(InfoSource::ReadKey,
              std::string(entry.key.protocol) + " " + std::to_string(entry.key.bits) + " bit",
              std::move(rows));
}

void BaseViewModel::raw_show_info() {
    if (raw_state_ != model::RawState::Stopped || !capture_) {
        return;
    }

    /* Same trimming as the saved file: a recording starts with a carrier-on period. */
    platform::PulseStats pulses;
    bool started = false;
    for (const int32_t duration_us : capture_->samples()) {
        started = started || duration_us > 0;
        if (started) {
            pulses.add(duration_us);
        }
    }

    std::vector<InfoRow> rows;
    info_sendable_ = false;
    add_signal_rows(rows, raw_frequency_hz_, platform::cc1101_preset_sub_file_name(raw_preset_));
    add_pulse_rows(rows, pulses);

    const auto dropped = capture_->dropped_edges();
    if (dropped > 0) {
        char text[96];
        std::snprintf(text,
                      sizeof(text),
                      "%llu edges lost: capture has gaps. 5 saves.",
                      static_cast<unsigned long long>(dropped));
        info_message_subject_.set(text);
    }
    else {
        info_message_subject_.set("Not saved yet. 5 saves this recording.");
    }
    open_info(InfoSource::RawRecording, "RAW recording", std::move(rows));
}

void BaseViewModel::saved_show_info() {
    const auto index = static_cast<std::size_t>(saved_selection_subject_.value());
    if (index >= saved_files_.size()) {
        return;
    }
    disarm_saved_delete();

    if (saved_files_[index].is_directory) {
        saved_path_ = saved_files_[index].path;
        saved_selection_subject_.set(0);
        refresh_saved();
        return;
    }

    std::string error;
    if (!show_file_info(saved_files_[index].path, error)) {
        saved_message_subject_.set((saved_files_[index].name + ": " + error).c_str());
    }
}

bool BaseViewModel::show_file_info(const std::string& path, std::string& error) {
    platform::CaptureDetails details;
    if (!platform::load_capture_details(path, details, error)) {
        return false;
    }
    info_file_path_ = path;
    info_file_name_ = file_name_of(info_file_path_);

    std::vector<InfoRow> rows;
    std::string title;
    info_sendable_ = true;
    std::string message = info_file_name_;
    if (details.raw) {
        title = "RAW recording";
        add_signal_rows(rows, details.frequency_hz, details.preset);
        add_pulse_rows(rows, details.pulses);
    }
    else {
        title = details.protocol + " " + std::to_string(details.bits) + " bit";
        rows.push_back({"Key", key_text(details.key, details.extra, details.extra_bits)});
        if (model::is_rolling_protocol(details.protocol)) {
            info_sendable_ = false;
            add_rolling_rows(rows, details.protocol, details.key);
            message += ": rolling code, cannot be sent";
        }
        add_signal_rows(rows, details.frequency_hz, details.preset);
        if (info_sendable_ && details.te_us > 0) {
            rows.push_back({"Pulse length", std::to_string(details.te_us) + " us"});
        }
    }

    info_message_subject_.set(message.c_str());
    open_info(InfoSource::SavedFile, std::move(title), std::move(rows));
    return true;
}

void BaseViewModel::info_save() {
    if (model_.current_page() != model::AppPage::Info) {
        return;
    }
    if (info_source_ == InfoSource::IrLearned) {
        ir_activate();
        return;
    }

    if (info_source_ == InfoSource::ReadKey) {
        std::string message;
        save_read_entry(info_read_entry_, message);
        info_message_subject_.set(message.c_str());
    }
    else if (info_source_ == InfoSource::RawRecording) {
        raw_save();
        if (raw_state_ == model::RawState::Stopped) {
            /* Still unsaved: raw_save left its error in the Read RAW message. */
            info_message_subject_.set(lv_subject_get_string(raw_message_subject_.native()));
        }
        else {
            /* Saved and cleared, so there is nothing left to summarise. */
            show_page(model::AppPage::ReadRaw);
        }
    }
}

void BaseViewModel::info_delete() {
    if (model_.current_page() == model::AppPage::Info && info_source_ == InfoSource::IrButton) {
        ir_delete();
        return;
    }
    if (model_.current_page() != model::AppPage::Info || info_source_ != InfoSource::SavedFile) {
        return;
    }

    if (!info_delete_armed_) {
        info_delete_armed_ = true;
        info_message_subject_.set(("Press 8 again to delete " + info_file_name_).c_str());
        return;
    }

    info_delete_armed_ = false;
    std::string error;
    if (!platform::delete_capture(info_file_path_, error)) {
        info_message_subject_.set(("Delete failed: " + error).c_str());
        return;
    }
    const std::string name = info_file_name_;
    show_page(model::AppPage::Saved);
    saved_message_subject_.set(("Deleted " + name).c_str());
}

bool BaseViewModel::prepare_file_transmit(const std::string& path, TransmitJob& job, std::string& message) {
    std::string error;
    platform::CaptureDetails details;
    if (!platform::load_capture_details(path, details, error)) {
        message = "Cannot send: " + error;
        return false;
    }
    /* A captured rolling code is never sent: not as a key, and not inside a recording. */
    if (!details.raw && model::is_rolling_protocol(details.protocol)) {
        message = "Rolling code: it cannot be sent";
        return false;
    }
    job.frequency_hz = details.frequency_hz;
    if (!preset_from_sub_file_name(details.preset, job.preset)) {
        message = "Cannot send: this modulation is not supported";
        return false;
    }
    const bool loaded =
        details.raw ? platform::load_raw_samples(path, kMaxTransmitPulses, job.pulses, error)
                    : model::encode_key(details.protocol, details.bits, details.key, details.te_us,
                                        job.pulses, error);
    if (!loaded) {
        message = "Cannot send: " + error;
        return false;
    }
    std::string rolling_protocol;
    if (details.raw && model::find_rolling_code(job.pulses, rolling_protocol)) {
        message = "Not sent: recording holds a " + rolling_protocol + " rolling code";
        return false;
    }
    return true;
}

void BaseViewModel::set_transmit_message(const std::string& message) {
    if (model_.current_page() == model::AppPage::Saved) {
        saved_message_subject_.set(message.c_str());
    }
    else {
        info_message_subject_.set(message.c_str());
    }
}

void BaseViewModel::start_transmit_job(TransmitJob job) {
    /* Checked here for a prompt answer, and again right before the carrier is turned on. */
    std::string reason;
    if (!model::transmit_allowed(job.frequency_hz, reason)) {
        set_transmit_message(reason);
        return;
    }

    job.pending = true;
    transmit_job_ = std::move(job);
    set_transmit_message("Preparing the radio...");
    sync_radio_activity();
}

void BaseViewModel::info_send() {
    if (model_.current_page() != model::AppPage::Info || transmit_job_.pending) {
        return;
    }
    info_delete_armed_ = false;
    if (info_source_ == InfoSource::IrLearned || info_source_ == InfoSource::IrButton) {
        ir_disarm_delete();
        ir_send(info_ir_signal_);
        return;
    }

    TransmitJob job;
    std::string message;
    if (info_source_ == InfoSource::ReadKey) {
        const auto& entry = info_read_entry_;
        if (model::is_rolling_protocol(entry.key.protocol)) {
            info_message_subject_.set("Rolling code: it cannot be sent");
            return;
        }
        job.frequency_hz = entry.frequency_hz;
        job.preset = entry.preset;
        if (!model::encode_key(entry.key.protocol, entry.key.bits, entry.key.key, entry.key.te_us,
                               job.pulses, message)) {
            info_message_subject_.set(message.c_str());
            return;
        }
    }
    else if (info_source_ == InfoSource::SavedFile) {
        if (!prepare_file_transmit(info_file_path_, job, message)) {
            info_message_subject_.set(message.c_str());
            return;
        }
    }
    else {
        info_message_subject_.set("Save the recording first (5), then send it from Saved");
        return;
    }

    start_transmit_job(std::move(job));
}

void BaseViewModel::saved_send() {
    if (model_.current_page() != model::AppPage::Saved || transmit_job_.pending) {
        return;
    }
    const auto index = static_cast<std::size_t>(saved_selection_subject_.value());
    if (index >= saved_files_.size()) {
        return;
    }
    if (saved_files_[index].is_directory) {
        /* There is nothing to send in a folder: Enter opens it, as C does. */
        saved_show_info();
        return;
    }
    saved_delete_armed_ = false;

    TransmitJob job;
    std::string message;
    if (!prepare_file_transmit(saved_files_[index].path, job, message)) {
        saved_message_subject_.set(message.c_str());
        return;
    }
    start_transmit_job(std::move(job));
}

} // namespace viewmodel
