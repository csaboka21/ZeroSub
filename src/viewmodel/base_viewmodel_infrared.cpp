/*
 * SPDX-License-Identifier: MIT
 *
 * BaseViewModel: the Infrared tool. Learn (receive and decode), Saved Remotes
 * (the .ir file browser) and Remote (the buttons of one file).
 */

#include "base_viewmodel.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <utility>

namespace viewmodel {
namespace {

/* Signals kept in the Learn list. */
constexpr std::size_t kMaxLearnEntries = 30;
/* How often the receiver is read and a running transmission is checked. */
constexpr uint32_t kIrPollPeriodMs = 30;
/* Carriers a raw signal may ask for; anything else is sent on the usual 38 kHz. */
constexpr uint32_t kMinCarrierHz = 10000;
constexpr uint32_t kMaxCarrierHz = 56000;
constexpr uint32_t kDefaultCarrierHz = 38000;

std::string file_name_of(const std::string& path) {
    return std::filesystem::path(path).filename().string();
}

std::string hex_text(uint32_t value, uint32_t bits, bool prefix) {
    char text[16];
    std::snprintf(text,
                  sizeof(text),
                  prefix ? "0x%0*X" : "%0*X",
                  static_cast<int>(std::max<uint32_t>((bits + 3) / 4, 2)),
                  static_cast<unsigned>(value));
    return text;
}

/* Address and command widths of a protocol; a file may name one this app does not know. */
void widths_of(const std::string& protocol, uint32_t& address_bits, uint32_t& command_bits) {
    if (!model::ir_protocol_widths(protocol, address_bits, command_bits)) {
        address_bits = 32;
        command_bits = 32;
    }
}

uint64_t length_us(const std::vector<uint32_t>& timings) {
    uint64_t total = 0;
    for (const uint32_t timing : timings) {
        total += timing;
    }
    return total;
}

/* "NEC  A:04 C:08", or "RAW  67 timings, 68 ms". */
std::string signal_text(const platform::IrSignal& signal) {
    if (signal.raw) {
        return "RAW  " + std::to_string(signal.timings.size()) + " timings, " +
               std::to_string(length_us(signal.timings) / 1000U) + " ms";
    }
    uint32_t address_bits = 0;
    uint32_t command_bits = 0;
    widths_of(signal.protocol, address_bits, command_bits);
    return signal.protocol + "  A:" + hex_text(signal.address, address_bits, false) + " C:" +
           hex_text(signal.command, command_bits, false);
}

/* The name a learned signal gets as a button: "NEC_08", or "RAW". */
std::string button_name(const platform::IrSignal& signal) {
    if (signal.raw) {
        return "RAW";
    }
    uint32_t address_bits = 0;
    uint32_t command_bits = 0;
    widths_of(signal.protocol, address_bits, command_bits);
    return signal.protocol + "_" + hex_text(signal.command, command_bits, false);
}

/* The file a single learned signal is saved to, without ".ir": "NEC_04_08". */
std::string file_base_name(const platform::IrSignal& signal) {
    if (signal.raw) {
        return platform::ir_timestamped_name("RAW");
    }
    uint32_t address_bits = 0;
    uint32_t command_bits = 0;
    widths_of(signal.protocol, address_bits, command_bits);
    return signal.protocol + "_" + hex_text(signal.address, address_bits, false) + "_" +
           hex_text(signal.command, command_bits, false);
}

} // namespace

lv_subject_t* BaseViewModel::ir_revision_subject() {
    return ir_revision_subject_.native();
}

lv_subject_t* BaseViewModel::ir_message_subject() {
    return ir_message_subject_.native();
}

lv_subject_t* BaseViewModel::ir_selection_subject(model::AppPage page) {
    switch (page) {
        case model::AppPage::IrSaved:
            return ir_saved_selection_subject_.native();
        case model::AppPage::IrRemote:
            return ir_remote_selection_subject_.native();
        default:
            return ir_learn_selection_subject_.native();
    }
}

std::size_t BaseViewModel::ir_list_count(model::AppPage page) const {
    switch (page) {
        case model::AppPage::IrSaved:
            return ir_files_.size();
        case model::AppPage::IrRemote:
            return ir_buttons_.size();
        default:
            return ir_learned_.size();
    }
}

std::string BaseViewModel::ir_list_text(model::AppPage page, std::size_t index) const {
    if (index >= ir_list_count(page)) {
        return std::string();
    }
    switch (page) {
        case model::AppPage::IrSaved:
            return ir_files_[index].is_directory ? ir_files_[index].name + "/" : ir_files_[index].name;
        case model::AppPage::IrRemote:
            return ir_buttons_[index].name;
        default:
            return signal_text(ir_learned_[index].signal) + "  x" + std::to_string(ir_learned_[index].repeats);
    }
}

std::string BaseViewModel::ir_list_summary(model::AppPage page) const {
    switch (page) {
        case model::AppPage::IrSaved: {
            const auto index = static_cast<std::size_t>(ir_saved_selection_subject_.value());
            return index < ir_files_.size() ? ir_files_[index].summary : std::string();
        }
        case model::AppPage::IrRemote: {
            const auto index = static_cast<std::size_t>(ir_remote_selection_subject_.value());
            return index < ir_buttons_.size() ? signal_text(ir_buttons_[index]) : std::string();
        }
        default:
            return ir_status_;
    }
}

void BaseViewModel::configure_infrared(const std::string& receive_device,
                                       const std::string& send_device,
                                       const std::string& directory) {
    ir_receive_device_ = receive_device;
    ir_send_device_ = send_device;
    ir_directory_ = directory;
}

std::string BaseViewModel::ir_directory() const {
    return ir_directory_.empty() ? platform::default_infrared_directory() : ir_directory_;
}

bool BaseViewModel::ir_in_subfolder() const {
    return !ir_path_.empty() && ir_path_ != ir_directory();
}

std::string BaseViewModel::ir_browse_directory() const {
    return ir_in_subfolder() ? ir_path_ : ir_directory();
}

void BaseViewModel::ir_touch() {
    ir_revision_subject_.set(ir_revision_subject_.value() + 1);
}

void BaseViewModel::ir_set_message(const std::string& message) {
    /* The summary page has its own message line. */
    if (model_.current_page() == model::AppPage::Info) {
        info_message_subject_.set(message.c_str());
    }
    else {
        ir_message_subject_.set(message.c_str());
    }
}

void BaseViewModel::ir_disarm_delete() {
    ir_delete_armed_ = false;
}

/* ---- Receiver, transmitter and their poll timer ---------------------------- */

void BaseViewModel::sync_ir_activity() {
    /* Leaving a page ends whatever it was sending. */
    ir_transmitter_.reset();
    ir_delete_armed_ = false;

    const model::AppPage page = model_.current_page();
    if (page == model::AppPage::IrLearn) {
        ir_message_subject_.set("");
        if (!ir_receiver_) {
            std::string error;
            ir_receiver_ = platform::IrReceiver::open(ir_receive_device_, error);
            ir_status_ = ir_receiver_ ? "Listening on " + ir_receiver_->path() : error;
        }
        ir_collector_.reset();
        ir_last_sample_tick_ = lv_tick_get();
        ir_touch();
    }
    else {
        ir_receiver_.reset();
        if (page == model::AppPage::IrRemote) {
            ir_message_subject_.set(file_name_of(ir_remote_path_).c_str());
            ir_touch();
        }
    }

    if (ir_receiver_ && !ir_timer_) {
        ir_timer_ = lv_timer_create(ir_timer_cb, kIrPollPeriodMs, this);
    }
    if (ir_timer_) {
        if (ir_receiver_) {
            lv_timer_resume(ir_timer_);
        }
        else {
            lv_timer_pause(ir_timer_);
        }
    }
}

void BaseViewModel::poll_ir() {
    if (ir_transmitter_ && !ir_transmitter_->running()) {
        const auto result = ir_transmitter_->result();
        ir_transmitter_.reset();
        if (result.completed) {
            ir_set_message("Sent " + ir_sending_name_);
        }
        else {
            ir_set_message(result.error.empty() ? "Sending stopped" : "Send failed: " + result.error);
        }
    }

    if (ir_receiver_) {
        std::vector<platform::IrSample> samples;
        std::string error;
        if (!ir_receiver_->read(samples, error)) {
            ir_receiver_.reset();
            ir_status_ = error;
            ir_touch();
        }
        else {
            std::vector<model::IrCapture> captures;
            for (const auto& sample : samples) {
                ir_collector_.feed(sample.mark, sample.duration_us, captures);
            }
            if (!samples.empty()) {
                ir_last_sample_tick_ = lv_tick_get();
            }
            else if (ir_collector_.busy()) {
                /* The receiver never reports the silence after the last mark. */
                const uint32_t silent_ms = lv_tick_elaps(ir_last_sample_tick_);
                ir_collector_.idle(silent_ms > 4000000U ? 4000000000U : silent_ms * 1000U, captures);
            }
            for (const auto& capture : captures) {
                ir_add_capture(capture);
            }
            if (!captures.empty()) {
                ir_touch();
            }
        }
    }

    if (!ir_receiver_ && !ir_transmitter_ && ir_timer_) {
        lv_timer_pause(ir_timer_);
    }
}

void BaseViewModel::ir_timer_cb(lv_timer_t* timer) {
    auto* view_model = static_cast<BaseViewModel*>(lv_timer_get_user_data(timer));
    if (view_model) {
        view_model->poll_ir();
    }
}

void BaseViewModel::ir_send(const platform::IrSignal& signal) {
    if (ir_transmitter_) {
        /* One at a time: the signal on the air finishes first. */
        return;
    }

    std::vector<uint32_t> timings;
    model::IrCarrier carrier;
    if (signal.raw) {
        timings = signal.timings;
        if (signal.frequency_hz >= kMinCarrierHz && signal.frequency_hz <= kMaxCarrierHz) {
            carrier.frequency_hz = signal.frequency_hz;
        }
        else {
            carrier.frequency_hz = kDefaultCarrierHz;
        }
        if (signal.duty_cycle > 0.0F && signal.duty_cycle < 1.0F) {
            carrier.duty_cycle = signal.duty_cycle;
        }
        if (timings.empty()) {
            ir_set_message("Cannot send: the signal is empty");
            return;
        }
    }
    else {
        std::string error;
        if (!model::ir_encode(signal.protocol, signal.address, signal.command, timings, carrier, error)) {
            ir_set_message("Cannot send: " + error);
            return;
        }
    }

    std::string error;
    ir_transmitter_ = platform::IrTransmitter::open(ir_send_device_, error);
    if (!ir_transmitter_) {
        ir_set_message(error);
        return;
    }

    ir_sending_name_ = signal.name.empty() ? signal_text(signal) : signal.name;
    ir_transmitter_->start(std::move(timings), carrier.frequency_hz, carrier.duty_cycle);
    ir_set_message("Sending " + ir_sending_name_ + "...");

    if (!ir_timer_) {
        ir_timer_ = lv_timer_create(ir_timer_cb, kIrPollPeriodMs, this);
    }
    if (ir_timer_) {
        lv_timer_resume(ir_timer_);
    }
}

/* ---- Learn ------------------------------------------------------------------ */

void BaseViewModel::ir_add_capture(const model::IrCapture& capture) {
    platform::IrSignal signal;
    signal.raw = capture.raw;
    if (capture.raw) {
        signal.timings = capture.timings;
        /* A demodulating receiver cannot tell the carrier; nearly every remote uses about 38 kHz. */
        signal.frequency_hz = kDefaultCarrierHz;
        signal.duty_cycle = 0.33F;
    }
    else {
        if (!capture.message.protocol) {
            return;
        }
        signal.protocol = capture.message.protocol;
        signal.address = capture.message.address;
        signal.command = capture.message.command;

        /* A held button repeats its message: count it on the newest entry instead of adding rows. */
        if (!ir_learned_.empty()) {
            auto& newest = ir_learned_.front();
            if (!newest.signal.raw && newest.signal.protocol == signal.protocol &&
                newest.signal.address == signal.address && newest.signal.command == signal.command) {
                ++newest.repeats;
                return;
            }
        }
    }

    ir_learned_.push_front({std::move(signal), 1});
    if (ir_learned_.size() > kMaxLearnEntries) {
        ir_learned_.pop_back();
    }
    /* Keep the cursor on the same signal, unless it was on the newest one. */
    const int32_t selection = ir_learn_selection_subject_.value();
    if (selection > 0) {
        ir_learn_selection_subject_.set(
            std::min<int32_t>(selection + 1, static_cast<int32_t>(ir_learned_.size()) - 1));
    }
    ir_message_subject_.set("");
}

bool BaseViewModel::ir_save_learned(const IrLearnEntry& entry, std::string& message) {
    platform::IrSignal signal = entry.signal;
    signal.name = button_name(signal);

    std::string path;
    std::string error;
    if (platform::save_new_ir_file(ir_directory(), file_base_name(signal), {signal}, path, error)) {
        message = "Saved " + file_name_of(path);
        return true;
    }
    message = "Save failed: " + error;
    return false;
}

void BaseViewModel::ir_save_all() {
    if (model_.current_page() != model::AppPage::IrLearn) {
        return;
    }
    if (ir_learned_.empty()) {
        ir_message_subject_.set("Nothing to save yet");
        return;
    }

    /* Oldest first, so the buttons are in the order they were pressed. */
    std::vector<platform::IrSignal> buttons;
    for (auto entry = ir_learned_.rbegin(); entry != ir_learned_.rend(); ++entry) {
        platform::IrSignal signal = entry->signal;
        const std::string base = button_name(signal);
        signal.name = base;
        for (int suffix = 2; std::any_of(buttons.begin(),
                                         buttons.end(),
                                         [&signal](const platform::IrSignal& other) {
                                             return other.name == signal.name;
                                         });
             ++suffix) {
            signal.name = base + "_" + std::to_string(suffix);
        }
        buttons.push_back(std::move(signal));
    }

    std::string path;
    std::string error;
    if (platform::save_new_ir_file(ir_directory(), platform::ir_timestamped_name("Remote"), buttons, path, error)) {
        ir_message_subject_.set(
            ("Saved " + std::to_string(buttons.size()) + " buttons as " + file_name_of(path)).c_str());
    }
    else {
        ir_message_subject_.set(("Save failed: " + error).c_str());
    }
}

/* ---- Saved Remotes ---------------------------------------------------------- */

void BaseViewModel::ir_refresh_files() {
    ir_delete_armed_ = false;

    /* A folder outside the library (the card was swapped, the config changed) falls back to its root. */
    const std::string root = ir_directory();
    if (ir_path_.compare(0, root.size(), root) != 0) {
        ir_path_.clear();
    }

    std::string error;
    /* Make sure the library exists, so there is a folder to copy remotes into before the first save. */
    platform::ensure_capture_directory(root, error);
    if (platform::list_directory(ir_browse_directory(), ".ir", ir_files_, error)) {
        ir_message_subject_.set(ir_browse_directory().c_str());
    }
    else {
        ir_message_subject_.set(error.c_str());
    }

    const auto last = ir_files_.empty() ? 0 : static_cast<int32_t>(ir_files_.size()) - 1;
    ir_saved_selection_subject_.set(std::clamp<int32_t>(ir_saved_selection_subject_.value(), 0, last));
    ir_describe_selection();
    ir_touch();
}

void BaseViewModel::ir_describe_selection() {
    /* Files are only read for the selected row, so large folders list quickly. */
    const auto index = static_cast<std::size_t>(ir_saved_selection_subject_.value());
    if (index < ir_files_.size() && ir_files_[index].summary.empty()) {
        auto& entry = ir_files_[index];
        entry.summary = entry.is_directory ? "Folder" : platform::describe_ir_file(entry.path);
    }
}

bool BaseViewModel::ir_up() {
    if (model_.current_page() != model::AppPage::IrSaved || !ir_in_subfolder()) {
        return false;
    }

    const std::filesystem::path current(ir_path_);
    const std::string left_folder = current.filename().string();
    ir_path_ = current.parent_path().string();
    ir_saved_selection_subject_.set(0);
    ir_refresh_files();

    /* Put the cursor back on the folder that was just left. */
    for (std::size_t index = 0; index < ir_files_.size(); ++index) {
        if (ir_files_[index].is_directory && ir_files_[index].name == left_folder) {
            ir_saved_selection_subject_.set(static_cast<int32_t>(index));
            ir_describe_selection();
            ir_touch();
            break;
        }
    }
    return true;
}

bool BaseViewModel::ir_open_remote(const std::string& path) {
    std::vector<platform::IrSignal> buttons;
    std::string error;
    if (!platform::load_ir_file(path, buttons, error)) {
        ir_message_subject_.set((file_name_of(path) + ": " + error).c_str());
        return false;
    }
    ir_remote_path_ = path;
    ir_buttons_ = std::move(buttons);
    ir_remote_selection_subject_.set(0);
    show_page(model::AppPage::IrRemote);
    return true;
}

bool BaseViewModel::ir_delete_button(std::size_t index, std::string& error) {
    if (index >= ir_buttons_.size()) {
        error = "no such button";
        return false;
    }
    std::vector<platform::IrSignal> remaining = ir_buttons_;
    remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(index));
    if (!platform::save_ir_file(ir_remote_path_, remaining, error)) {
        return false;
    }
    ir_buttons_ = std::move(remaining);
    const auto last = ir_buttons_.empty() ? 0 : static_cast<int32_t>(ir_buttons_.size()) - 1;
    ir_remote_selection_subject_.set(std::clamp<int32_t>(ir_remote_selection_subject_.value(), 0, last));
    return true;
}

/* ---- Keys ------------------------------------------------------------------- */

void BaseViewModel::ir_next() {
    const model::AppPage page = model_.current_page();
    const auto count = static_cast<int32_t>(ir_list_count(page));
    if (count == 0) {
        return;
    }
    if (ir_delete_armed_) {
        ir_delete_armed_ = false;
        ir_message_subject_.set(file_name_of(ir_remote_path_).c_str());
    }
    auto* selection = ir_selection_subject(page);
    lv_subject_set_int(selection, std::min(lv_subject_get_int(selection) + 1, count - 1));
    if (page == model::AppPage::IrSaved) {
        ir_describe_selection();
        ir_message_subject_.set(ir_browse_directory().c_str());
    }
    ir_touch();
}

void BaseViewModel::ir_previous() {
    const model::AppPage page = model_.current_page();
    if (ir_delete_armed_) {
        ir_delete_armed_ = false;
        ir_message_subject_.set(file_name_of(ir_remote_path_).c_str());
    }
    auto* selection = ir_selection_subject(page);
    lv_subject_set_int(selection, std::max<int32_t>(lv_subject_get_int(selection) - 1, 0));
    if (page == model::AppPage::IrSaved) {
        ir_describe_selection();
        ir_message_subject_.set(ir_browse_directory().c_str());
    }
    ir_touch();
}

void BaseViewModel::ir_show_info(InfoSource source, const platform::IrSignal& signal, uint32_t repeats) {
    info_ir_signal_ = signal;

    std::vector<InfoRow> rows;
    std::string title;
    std::string message;
    char text[32];
    if (source == InfoSource::IrButton) {
        rows.push_back({"Button", signal.name});
        message = file_name_of(ir_remote_path_);
    }
    else {
        message = "ENTER sends this signal, 5 saves it.";
    }

    if (signal.raw) {
        title = "RAW signal";
        info_sendable_ = !signal.timings.empty();
        std::snprintf(text,
                      sizeof(text),
                      "%u.%u kHz",
                      static_cast<unsigned>(signal.frequency_hz / 1000U),
                      static_cast<unsigned>((signal.frequency_hz % 1000U) / 100U));
        rows.push_back({"Carrier", text});
        std::snprintf(text, sizeof(text), "%d %%", static_cast<int>(signal.duty_cycle * 100.0F + 0.5F));
        rows.push_back({"Duty cycle", text});
        rows.push_back({"Timings", std::to_string(signal.timings.size())});
        rows.push_back({"Length", std::to_string(length_us(signal.timings) / 1000U) + " ms"});
    }
    else {
        title = signal.protocol;
        uint32_t address_bits = 0;
        uint32_t command_bits = 0;
        widths_of(signal.protocol, address_bits, command_bits);
        rows.push_back({"Address", hex_text(signal.address, address_bits, true)});
        rows.push_back({"Command", hex_text(signal.command, command_bits, true)});

        std::vector<uint32_t> timings;
        model::IrCarrier carrier;
        std::string error;
        info_sendable_ =
            model::ir_encode(signal.protocol, signal.address, signal.command, timings, carrier, error);
        if (info_sendable_) {
            std::snprintf(text, sizeof(text), "%u kHz", static_cast<unsigned>(carrier.frequency_hz / 1000U));
            rows.push_back({"Carrier", text});
        }
        else {
            message = signal.protocol + ": unknown protocol, it cannot be sent";
        }
        if (source == InfoSource::IrLearned) {
            rows.push_back({"Seen", std::to_string(repeats) + (repeats == 1 ? " time" : " times")});
        }
    }

    info_message_subject_.set(message.c_str());
    open_info(source, std::move(title), std::move(rows));
}

void BaseViewModel::ir_open() {
    switch (model_.current_page()) {
        case model::AppPage::IrLearn: {
            const auto index = static_cast<std::size_t>(ir_learn_selection_subject_.value());
            if (index < ir_learned_.size()) {
                ir_show_info(InfoSource::IrLearned, ir_learned_[index].signal, ir_learned_[index].repeats);
            }
            break;
        }
        case model::AppPage::IrSaved: {
            const auto index = static_cast<std::size_t>(ir_saved_selection_subject_.value());
            if (index >= ir_files_.size()) {
                break;
            }
            ir_delete_armed_ = false;
            if (ir_files_[index].is_directory) {
                ir_path_ = ir_files_[index].path;
                ir_saved_selection_subject_.set(0);
                ir_refresh_files();
            }
            else {
                ir_open_remote(ir_files_[index].path);
            }
            break;
        }
        case model::AppPage::IrRemote: {
            const auto index = static_cast<std::size_t>(ir_remote_selection_subject_.value());
            if (index < ir_buttons_.size()) {
                info_ir_index_ = index;
                ir_show_info(InfoSource::IrButton, ir_buttons_[index], 0);
            }
            break;
        }
        default:
            break;
    }
}

void BaseViewModel::ir_activate() {
    switch (model_.current_page()) {
        case model::AppPage::IrLearn: {
            const auto index = static_cast<std::size_t>(ir_learn_selection_subject_.value());
            if (index < ir_learned_.size()) {
                std::string message;
                ir_save_learned(ir_learned_[index], message);
                ir_message_subject_.set(message.c_str());
            }
            break;
        }
        case model::AppPage::IrSaved:
            ir_open();
            break;
        case model::AppPage::IrRemote: {
            const auto index = static_cast<std::size_t>(ir_remote_selection_subject_.value());
            if (index < ir_buttons_.size()) {
                ir_delete_armed_ = false;
                ir_send(ir_buttons_[index]);
            }
            break;
        }
        case model::AppPage::Info:
            /* 5 on the summary of a learned signal. */
            if (info_source_ == InfoSource::IrLearned) {
                IrLearnEntry entry;
                entry.signal = info_ir_signal_;
                std::string message;
                ir_save_learned(entry, message);
                info_message_subject_.set(message.c_str());
            }
            break;
        default:
            break;
    }
}

void BaseViewModel::ir_delete() {
    const model::AppPage page = model_.current_page();

    if (page == model::AppPage::IrSaved) {
        const auto index = static_cast<std::size_t>(ir_saved_selection_subject_.value());
        if (index >= ir_files_.size()) {
            return;
        }
        if (ir_files_[index].is_directory) {
            ir_message_subject_.set("Folders cannot be deleted here");
            return;
        }
        const std::string name = ir_files_[index].name;
        if (!ir_delete_armed_) {
            ir_delete_armed_ = true;
            ir_message_subject_.set(("Press 8 again to delete " + name).c_str());
            return;
        }
        std::string error;
        const bool deleted = platform::delete_ir_file(ir_files_[index].path, error);
        ir_refresh_files();
        ir_message_subject_.set(deleted ? ("Deleted " + name).c_str() : ("Delete failed: " + error).c_str());
        return;
    }

    const bool on_info = page == model::AppPage::Info && info_source_ == InfoSource::IrButton;
    if (page != model::AppPage::IrRemote && !on_info) {
        return;
    }
    const auto index = on_info ? info_ir_index_
                               : static_cast<std::size_t>(ir_remote_selection_subject_.value());
    if (index >= ir_buttons_.size() || ir_transmitter_) {
        return;
    }
    const std::string name = ir_buttons_[index].name;
    if (!ir_delete_armed_) {
        ir_delete_armed_ = true;
        ir_set_message("Press 8 again to delete button " + name);
        return;
    }
    ir_delete_armed_ = false;
    std::string error;
    if (!ir_delete_button(index, error)) {
        ir_set_message("Delete failed: " + error);
        return;
    }
    if (on_info) {
        /* The button is gone, so there is nothing left to summarise. */
        show_page(model::AppPage::IrRemote);
    }
    ir_message_subject_.set(("Deleted button " + name).c_str());
    ir_touch();
}

} // namespace viewmodel
