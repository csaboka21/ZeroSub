/*
 * SPDX-License-Identifier: MIT
 *
 * BaseViewModel: the 125 kHz RFID tool. Saved Keys (the .rfid file browser),
 * Key (one saved key: emulate it on the coil front-end) and Add Manually (a
 * new EM4100 ID).
 */

#include "base_viewmodel.h"

#include "rfid_store.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <random>
#include <utility>

namespace viewmodel {
namespace {

/* How often a running emulation is checked for news. */
constexpr uint32_t kRfidPollPeriodMs = 200;

/* Digits of an EM4100 ID: two per byte. */
constexpr std::size_t kRfidDigits = model::kEm4100DataSize * 2;

std::string file_name_of(const std::string& path) {
    return std::filesystem::path(path).filename().string();
}

std::vector<std::string> key_lines(const model::RfidKey& key, bool inverted) {
    std::vector<std::string> lines;
    lines.push_back("Type  " + key.type);
    lines.push_back("ID  " + model::rfid_hex(key.data));
    const uint32_t cycles = model::em4100_cycles_per_bit(key.type);
    if (cycles == 0 || key.data.size() != model::kEm4100DataSize) {
        lines.push_back("Only EM4100 keys can be emulated");
        return lines;
    }
    lines.push_back("Card  " + model::em4100_card_number(key.data));
    lines.push_back("FC,Card  " + model::em4100_facility_card(key.data));
    char rate[48];
    std::snprintf(rate,
                  sizeof(rate),
                  "RF/%u, %.1f ms per frame",
                  static_cast<unsigned>(cycles),
                  static_cast<double>(cycles * model::kRfidCycleUs * model::kEm4100FrameBits) / 1000.0);
    lines.push_back(rate);
    lines.push_back(inverted ? "Polarity  inverted (I)" : "Polarity  normal (I swaps)");
    return lines;
}

/* "01 23 [4]5 67 89": the ID with the digit under the cursor in brackets. */
std::string add_id_text(const std::array<uint8_t, model::kEm4100DataSize>& id, std::size_t cursor) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string text;
    for (std::size_t digit = 0; digit < kRfidDigits; ++digit) {
        if (digit != 0 && digit % 2 == 0) {
            text += ' ';
        }
        const uint8_t byte = id[digit / 2];
        const char letter = kHex[digit % 2 == 0 ? byte >> 4 : byte & 0x0F];
        if (digit == cursor) {
            text += '[';
            text += letter;
            text += ']';
        }
        else {
            text += letter;
        }
    }
    return text;
}

} // namespace

lv_subject_t* BaseViewModel::rfid_revision_subject() {
    return rfid_revision_subject_.native();
}

lv_subject_t* BaseViewModel::rfid_message_subject() {
    return rfid_message_subject_.native();
}

lv_subject_t* BaseViewModel::rfid_selection_subject() {
    return rfid_saved_selection_subject_.native();
}

std::size_t BaseViewModel::rfid_list_count(model::AppPage page) const {
    switch (page) {
        case model::AppPage::RfidSaved:
            return rfid_files_.size();
        case model::AppPage::RfidKey:
            return rfid_key_lines_.size();
        case model::AppPage::RfidAdd:
            return 4;
        case model::AppPage::RfidRead:
            return rfid_read_lines_.size();
        default:
            return 0;
    }
}

std::string BaseViewModel::rfid_list_text(model::AppPage page, std::size_t index) const {
    if (index >= rfid_list_count(page)) {
        return std::string();
    }
    switch (page) {
        case model::AppPage::RfidSaved:
            return rfid_files_[index].is_directory ? rfid_files_[index].name + "/" : rfid_files_[index].name;
        case model::AppPage::RfidKey:
            return rfid_key_lines_[index];
        case model::AppPage::RfidRead:
            return rfid_read_lines_[index];
        case model::AppPage::RfidAdd: {
            const std::vector<uint8_t> data(rfid_add_id_.begin(), rfid_add_id_.end());
            switch (index) {
                case 0:
                    return "Type  EM4100";
                case 1:
                    return "ID  " + add_id_text(rfid_add_id_, rfid_add_cursor_);
                case 2:
                    return "Card  " + model::em4100_card_number(data);
                default:
                    return "FC,Card  " + model::em4100_facility_card(data);
            }
        }
        default:
            return std::string();
    }
}

std::size_t BaseViewModel::rfid_selection(model::AppPage page) const {
    switch (page) {
        case model::AppPage::RfidSaved:
            return static_cast<std::size_t>(rfid_saved_selection_subject_.value());
        case model::AppPage::RfidKey:
            return rfid_key_line_;
        case model::AppPage::RfidRead:
            return rfid_read_line_;
        default:
            /* No highlighted row: the cursor is the bracketed digit. */
            return rfid_list_count(page);
    }
}

std::string BaseViewModel::rfid_list_summary(model::AppPage page) const {
    if (page == model::AppPage::RfidSaved) {
        const auto index = static_cast<std::size_t>(rfid_saved_selection_subject_.value());
        return index < rfid_files_.size() ? rfid_files_[index].summary : std::string();
    }
    return rfid_status_;
}

void BaseViewModel::configure_rfid(const platform::RfidConfig& config, const std::string& directory) {
    rfid_stop();
    rfid_config_ = config;
    rfid_directory_ = directory;
}

std::string BaseViewModel::rfid_directory() const {
    return rfid_directory_.empty() ? platform::default_rfid_directory() : rfid_directory_;
}

bool BaseViewModel::rfid_in_subfolder() const {
    return !rfid_path_.empty() && rfid_path_ != rfid_directory();
}

std::string BaseViewModel::rfid_browse_directory() const {
    return rfid_in_subfolder() ? rfid_path_ : rfid_directory();
}

void BaseViewModel::rfid_touch() {
    rfid_revision_subject_.set(rfid_revision_subject_.value() + 1);
}

/* ---- The emulator and its poll timer --------------------------------------- */

void BaseViewModel::rfid_idle_status() {
    if (!model::rfid_can_emulate(rfid_key_)) {
        rfid_status_ = rfid_key_.type + ": shown and saved only";
    }
    else {
        rfid_status_ = "ENTER emulates. Remove the Cap first";
    }
}

void BaseViewModel::rfid_start() {
    rfid_stop();
    if (!model::rfid_can_emulate(rfid_key_)) {
        rfid_status_ = "Only EM4100 keys can be emulated";
        rfid_touch();
        return;
    }

    std::array<uint8_t, model::kEm4100DataSize> id{};
    std::copy(rfid_key_.data.begin(), rfid_key_.data.end(), id.begin());
    const uint32_t half_bit_us = model::em4100_cycles_per_bit(rfid_key_.type) * model::kRfidCycleUs / 2;

    /*
     * The coil driver runs from the EXT 5 V rail. While emulating it holds its
     * output steady (the carrier UART is idle), which closes the coil's
     * resonant loop; unpowered, it would leave the loop open.
     */
    std::string error;
    if (!cap_power_.enabled() && !cap_power_.enable(error)) {
        rfid_status_ = "5 V: " + error;
        rfid_touch();
        return;
    }
    rfid_emulator_ =
        platform::RfidEmulator::start(rfid_config_, model::em4100_half_bits(id, rfid_inverted_), half_bit_us, error);
    if (!rfid_emulator_) {
        rfid_status_ = error;
        rfid_touch();
        return;
    }
    rfid_seen_revision_ = rfid_emulator_->revision() - 1;
    if (!rfid_timer_) {
        rfid_timer_ = lv_timer_create(rfid_timer_cb, kRfidPollPeriodMs, this);
    }
    if (rfid_timer_) {
        lv_timer_resume(rfid_timer_);
    }
    poll_rfid();
}

void BaseViewModel::rfid_stop() {
    /* Joins the workers, which leave the coil unloaded, the relay in tag mode and the pins handed back. */
    rfid_emulator_.reset();
    rfid_reader_.reset();
    if (rfid_timer_) {
        lv_timer_pause(rfid_timer_);
    }
}

/* ---- Read Key ---------------------------------------------------------------- */

void BaseViewModel::rfid_start_reader() {
    rfid_stop();
    rfid_read_valid_ = false;
    rfid_read_lines_.clear();
    rfid_read_line_ = 0;

    /* The relay, the driver and the comparator run from the EXT 5 V rail. */
    std::string error;
    if (!cap_power_.enabled() && !cap_power_.enable(error)) {
        rfid_status_ = "5 V: " + error;
        rfid_touch();
        return;
    }
    rfid_reader_ = platform::RfidReader::start(rfid_config_, error);
    if (!rfid_reader_) {
        rfid_status_ = error;
        rfid_touch();
        return;
    }
    rfid_status_ = "Hold an EM4100 tag on the coil";
    rfid_reader_seen_revision_ = rfid_reader_->revision() - 1;
    if (!rfid_timer_) {
        rfid_timer_ = lv_timer_create(rfid_timer_cb, kRfidPollPeriodMs, this);
    }
    if (rfid_timer_) {
        lv_timer_resume(rfid_timer_);
    }
    poll_rfid_reader();
}

void BaseViewModel::poll_rfid_reader() {
    const uint32_t revision = rfid_reader_->revision();
    if (revision == rfid_reader_seen_revision_) {
        return;
    }
    rfid_reader_seen_revision_ = revision;
    const auto status = rfid_reader_->status();

    char text[96];
    if (!status.error.empty()) {
        rfid_status_ = status.error;
    }
    else if (status.have_id) {
        std::snprintf(text, sizeof(text), "Read (%lu frames)", static_cast<unsigned long>(status.frames));
        rfid_status_ = text;
    }
    else {
        std::snprintf(text,
                      sizeof(text),
                      "Listening: %llu edges, %lu frames",
                      static_cast<unsigned long long>(status.edges),
                      static_cast<unsigned long>(status.frames));
        rfid_status_ = text;
    }

    const std::vector<uint8_t> data(status.id.begin(), status.id.end());
    if (status.have_id && (!rfid_read_valid_ || rfid_read_key_.data != data)) {
        rfid_read_valid_ = true;
        rfid_read_key_ = model::RfidKey{};
        rfid_read_key_.data = data;
        rfid_read_lines_ = {
            "ID  " + model::rfid_hex(data),
            "Card  " + model::em4100_card_number(data),
            "FC,Card  " + model::em4100_facility_card(data),
        };
        rfid_read_line_ = 0;
        rfid_message_subject_.set("ENTER saves the key, 5 reads another one");
    }
    else if (!rfid_read_valid_) {
        /* The rate the UART driver actually set: 250000 means a true 125 kHz carrier. */
        std::snprintf(text, sizeof(text), "Carrier: UART at %lu baud", static_cast<unsigned long>(status.carrier_baud));
        rfid_message_subject_.set(text);
    }
    rfid_touch();
}

void BaseViewModel::rfid_read_again() {
    if (model_.current_page() != model::AppPage::RfidRead) {
        return;
    }
    rfid_read_valid_ = false;
    rfid_read_lines_.clear();
    rfid_read_line_ = 0;
    if (rfid_reader_) {
        rfid_reader_->clear();
        rfid_status_ = "Hold an EM4100 tag on the coil";
        rfid_touch();
    }
    else {
        rfid_start_reader();
    }
}

void BaseViewModel::poll_rfid() {
    if (rfid_reader_) {
        poll_rfid_reader();
        return;
    }
    if (!rfid_emulator_) {
        if (rfid_timer_) {
            lv_timer_pause(rfid_timer_);
        }
        return;
    }
    const uint32_t revision = rfid_emulator_->revision();
    if (revision == rfid_seen_revision_) {
        return;
    }
    rfid_seen_revision_ = revision;

    const auto status = rfid_emulator_->status();
    using Phase = platform::RfidEmulator::Phase;
    char text[96];
    switch (status.phase) {
        case Phase::Waiting:
            std::snprintf(text,
                          sizeof(text),
                          "Waiting for a reader (%lu frames sent)",
                          static_cast<unsigned long>(status.frames));
            rfid_status_ = text;
            break;
        case Phase::Sending:
            std::snprintf(text,
                          sizeof(text),
                          "Sending: %lu frames, up to %lu us late",
                          static_cast<unsigned long>(status.frames),
                          static_cast<unsigned long>(status.worst_late_us));
            rfid_status_ = text;
            break;
        case Phase::Failed:
            rfid_status_ = "Stopped: " + status.error;
            rfid_stop();
            break;
    }
    /* Without field detect the key goes out all the time; the line below says why. */
    if (!status.field_detect) {
        rfid_message_subject_.set(status.field_note.c_str());
    }
    rfid_touch();
}

void BaseViewModel::rfid_timer_cb(lv_timer_t* timer) {
    auto* view_model = static_cast<BaseViewModel*>(lv_timer_get_user_data(timer));
    if (view_model) {
        view_model->poll_rfid();
    }
}

void BaseViewModel::sync_rfid_activity() {
    rfid_delete_armed_ = false;
    /* Nothing runs until asked: emulating starts with a key press on the Key page. */
    rfid_stop();
    switch (model_.current_page()) {
        case model::AppPage::RfidKey:
            rfid_idle_status();
            rfid_message_subject_.set(file_name_of(rfid_key_path_).c_str());
            rfid_touch();
            break;
        case model::AppPage::RfidRead:
            rfid_message_subject_.set("");
            rfid_start_reader();
            rfid_touch();
            break;
        case model::AppPage::RfidAdd:
            if (!rfid_add_initialized_) {
                rfid_randomize();
                rfid_add_initialized_ = true;
            }
            rfid_status_ = "ENTER saves the key";
            rfid_message_subject_.set("F/X change the digit, Z/C move, R random");
            rfid_touch();
            break;
        default:
            break;
    }
}

/* ---- Saved Keys --------------------------------------------------------------- */

void BaseViewModel::rfid_refresh_files() {
    rfid_delete_armed_ = false;

    /* A folder outside the library (the card was swapped, the config changed) falls back to its root. */
    const std::string root = rfid_directory();
    if (rfid_path_.compare(0, root.size(), root) != 0) {
        rfid_path_.clear();
    }

    std::string error;
    platform::ensure_capture_directory(root, error);
    if (platform::list_directory(rfid_browse_directory(), ".rfid", rfid_files_, error)) {
        rfid_message_subject_.set(rfid_browse_directory().c_str());
    }
    else {
        rfid_message_subject_.set(error.c_str());
    }

    const auto last = rfid_files_.empty() ? 0 : static_cast<int32_t>(rfid_files_.size()) - 1;
    rfid_saved_selection_subject_.set(std::clamp<int32_t>(rfid_saved_selection_subject_.value(), 0, last));
    rfid_describe_selection();
    rfid_touch();
}

void BaseViewModel::rfid_describe_selection() {
    const auto index = static_cast<std::size_t>(rfid_saved_selection_subject_.value());
    if (index < rfid_files_.size() && rfid_files_[index].summary.empty()) {
        auto& entry = rfid_files_[index];
        entry.summary = entry.is_directory ? "Folder" : platform::describe_rfid_file(entry.path);
    }
}

bool BaseViewModel::rfid_up() {
    if (model_.current_page() != model::AppPage::RfidSaved || !rfid_in_subfolder()) {
        return false;
    }

    const std::filesystem::path current(rfid_path_);
    const std::string left_folder = current.filename().string();
    rfid_path_ = current.parent_path().string();
    rfid_saved_selection_subject_.set(0);
    rfid_refresh_files();

    /* Put the cursor back on the folder that was just left. */
    for (std::size_t index = 0; index < rfid_files_.size(); ++index) {
        if (rfid_files_[index].is_directory && rfid_files_[index].name == left_folder) {
            rfid_saved_selection_subject_.set(static_cast<int32_t>(index));
            rfid_describe_selection();
            rfid_touch();
            break;
        }
    }
    return true;
}

bool BaseViewModel::rfid_open_key(const std::string& path) {
    model::RfidKey key;
    std::string error;
    if (!platform::load_rfid_file(path, key, error)) {
        rfid_message_subject_.set((file_name_of(path) + ": " + error).c_str());
        return false;
    }
    rfid_key_path_ = path;
    rfid_key_ = std::move(key);
    rfid_inverted_ = false;
    rfid_key_lines_ = key_lines(rfid_key_, rfid_inverted_);
    rfid_key_line_ = 0;
    show_page(model::AppPage::RfidKey);
    return true;
}

void BaseViewModel::rfid_step_selection(int32_t step) {
    const model::AppPage page = model_.current_page();
    if (page == model::AppPage::RfidKey || page == model::AppPage::RfidRead) {
        std::size_t& line = page == model::AppPage::RfidKey ? rfid_key_line_ : rfid_read_line_;
        const std::size_t count = rfid_list_count(page);
        if (step > 0 && line + 1 < count) {
            ++line;
        }
        else if (step < 0 && line > 0) {
            --line;
        }
        rfid_touch();
        return;
    }
    if (page == model::AppPage::RfidAdd) {
        /* Up raises the digit under the cursor, down lowers it, wrapping round. */
        uint8_t& byte = rfid_add_id_[rfid_add_cursor_ / 2];
        const bool high = rfid_add_cursor_ % 2 == 0;
        uint8_t digit = high ? static_cast<uint8_t>(byte >> 4) : static_cast<uint8_t>(byte & 0x0F);
        digit = static_cast<uint8_t>((digit + (step < 0 ? 1 : 15)) & 0x0F);
        byte = high ? static_cast<uint8_t>((byte & 0x0F) | (digit << 4)) : static_cast<uint8_t>((byte & 0xF0) | digit);
        rfid_touch();
        return;
    }
    if (page != model::AppPage::RfidSaved || rfid_files_.empty()) {
        return;
    }
    rfid_delete_armed_ = false;
    const auto last = static_cast<int32_t>(rfid_files_.size()) - 1;
    rfid_saved_selection_subject_.set(std::clamp<int32_t>(rfid_saved_selection_subject_.value() + step, 0, last));
    rfid_describe_selection();
    rfid_message_subject_.set(rfid_browse_directory().c_str());
    rfid_touch();
}

void BaseViewModel::rfid_next() {
    rfid_step_selection(1);
}

void BaseViewModel::rfid_previous() {
    rfid_step_selection(-1);
}

void BaseViewModel::rfid_cursor(bool right) {
    if (model_.current_page() != model::AppPage::RfidAdd) {
        return;
    }
    rfid_add_cursor_ = (rfid_add_cursor_ + (right ? 1 : kRfidDigits - 1)) % kRfidDigits;
    rfid_touch();
}

void BaseViewModel::rfid_randomize() {
    std::random_device random;
    for (auto& byte : rfid_add_id_) {
        byte = static_cast<uint8_t>(random());
    }
    rfid_touch();
}

void BaseViewModel::rfid_open() {
    if (model_.current_page() != model::AppPage::RfidSaved) {
        return;
    }
    const auto index = static_cast<std::size_t>(rfid_saved_selection_subject_.value());
    if (index >= rfid_files_.size()) {
        return;
    }
    rfid_delete_armed_ = false;
    if (rfid_files_[index].is_directory) {
        rfid_path_ = rfid_files_[index].path;
        rfid_saved_selection_subject_.set(0);
        rfid_refresh_files();
    }
    else {
        rfid_open_key(rfid_files_[index].path);
    }
}

/* ---- Keys ------------------------------------------------------------------- */

void BaseViewModel::rfid_activate() {
    switch (model_.current_page()) {
        case model::AppPage::RfidSaved:
            rfid_open();
            break;

        case model::AppPage::RfidKey:
            rfid_delete_armed_ = false;
            if (rfid_emulator_) {
                rfid_stop();
                rfid_status_ = "Stopped";
            }
            else {
                rfid_message_subject_.set(file_name_of(rfid_key_path_).c_str());
                rfid_start();
            }
            rfid_touch();
            break;

        case model::AppPage::RfidRead: {
            if (!rfid_read_valid_) {
                break;
            }
            std::string path;
            std::string error;
            if (!platform::save_new_rfid_file(rfid_directory(), rfid_read_key_, path, error)) {
                rfid_message_subject_.set(("Save failed: " + error).c_str());
                break;
            }
            /* The saved key opens straight away, ready to emulate; that also stops the reader. */
            rfid_path_.clear();
            if (rfid_open_key(path)) {
                rfid_message_subject_.set(("Saved " + file_name_of(path)).c_str());
            }
            break;
        }

        case model::AppPage::RfidAdd: {
            model::RfidKey key;
            key.data.assign(rfid_add_id_.begin(), rfid_add_id_.end());
            std::string path;
            std::string error;
            if (!platform::save_new_rfid_file(rfid_directory(), key, path, error)) {
                rfid_message_subject_.set(("Save failed: " + error).c_str());
                break;
            }
            /* The next new key starts from a fresh random ID. */
            rfid_add_initialized_ = false;
            rfid_path_.clear();
            if (rfid_open_key(path)) {
                rfid_message_subject_.set(("Saved " + file_name_of(path)).c_str());
            }
            break;
        }

        default:
            break;
    }
}

void BaseViewModel::rfid_invert() {
    if (model_.current_page() != model::AppPage::RfidKey) {
        return;
    }
    rfid_delete_armed_ = false;
    rfid_inverted_ = !rfid_inverted_;
    rfid_key_lines_ = key_lines(rfid_key_, rfid_inverted_);
    rfid_key_line_ = std::min(rfid_key_line_, rfid_key_lines_.empty() ? 0 : rfid_key_lines_.size() - 1);
    /* A running emulation carries on with the new polarity. */
    if (rfid_emulator_) {
        rfid_start();
    }
    rfid_message_subject_.set(rfid_inverted_ ? "Polarity inverted" : "Polarity normal");
    rfid_touch();
}

bool BaseViewModel::rfid_stop_job() {
    if (model_.current_page() != model::AppPage::RfidKey || !rfid_emulator_) {
        return false;
    }
    rfid_stop();
    rfid_status_ = "Stopped";
    rfid_touch();
    return true;
}

void BaseViewModel::rfid_delete() {
    const model::AppPage page = model_.current_page();
    std::string path;
    if (page == model::AppPage::RfidSaved) {
        const auto index = static_cast<std::size_t>(rfid_saved_selection_subject_.value());
        if (index >= rfid_files_.size()) {
            return;
        }
        if (rfid_files_[index].is_directory) {
            rfid_message_subject_.set("Folders cannot be deleted here");
            return;
        }
        path = rfid_files_[index].path;
    }
    else if (page == model::AppPage::RfidKey && !rfid_emulator_) {
        path = rfid_key_path_;
    }
    else {
        return;
    }

    if (!rfid_delete_armed_) {
        rfid_delete_armed_ = true;
        rfid_message_subject_.set(("Press 8 again to delete " + file_name_of(path)).c_str());
        return;
    }
    rfid_delete_armed_ = false;

    std::string error;
    if (!platform::delete_rfid_file(path, error)) {
        rfid_message_subject_.set(("Delete failed: " + error).c_str());
        return;
    }
    const std::string deleted = "Deleted " + file_name_of(path);
    if (page == model::AppPage::RfidKey) {
        show_page(model::AppPage::RfidSaved);
    }
    else {
        rfid_refresh_files();
    }
    rfid_message_subject_.set(deleted.c_str());
}

} // namespace viewmodel
