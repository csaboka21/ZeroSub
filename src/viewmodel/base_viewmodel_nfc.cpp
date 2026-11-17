/*
 * SPDX-License-Identifier: MIT
 *
 * BaseViewModel: the NFC tool. Read (wait for a tag and read it), Saved Tags
 * (the .nfc file browser) and Tag (one saved tag: emulate it, or write it to
 * another tag).
 */

#include "base_viewmodel.h"

#include "nfc/amiibo.h"
#include "nfc/ndef.h"
#include "nfc_store.h"

#include <algorithm>
#include <cstdio>
#include <array>
#include <filesystem>
#include <random>
#include <utility>

namespace viewmodel {
namespace {

/* How often a running NFC job is checked for news. */
constexpr uint32_t kNfcPollPeriodMs = 100;

constexpr const char* kCardIdleStatus = "ENTER emulates, 6 writes to a tag";
constexpr const char* kUidOnlyIdleStatus = "ENTER emulates the UID";

const char* card_idle_status(const model::NfcTag& tag) {
    if (tag.technology == model::NfcTechnology::V) {
        return "NFC-V tag: shown and saved only";
    }
    if (tag.technology == model::NfcTechnology::B) {
        return "NFC-B tag: shown and saved only";
    }
    if (tag.technology == model::NfcTechnology::F) {
        return "FeliCa card: shown and saved only";
    }
    return tag.type2 ? kCardIdleStatus : kUidOnlyIdleStatus;
}

std::string file_name_of(const std::string& path) {
    return std::filesystem::path(path).filename().string();
}

/* Roughly what fits on one row, counted in Latin letters; wide characters count double. */
constexpr std::size_t kLineColumns = 44;
/* Rows one record may take; a longer value ends in "...". */
constexpr std::size_t kMaxRecordLines = 6;

/*
 * Adds "label  value", continued on indented rows when it is too long for
 * one. Rows are cut between characters, never inside a UTF-8 sequence.
 */
void add_wrapped(std::vector<std::string>& lines, const std::string& label, const std::string& value) {
    std::string line = label + "  ";
    std::size_t columns = line.size();
    std::size_t used = 1;
    std::size_t offset = 0;
    while (offset < value.size()) {
        std::size_t length = 1;
        while (offset + length < value.size() &&
               (static_cast<unsigned char>(value[offset + length]) & 0xC0) == 0x80) {
            ++length;
        }
        /* Three-byte sequences and up are mostly CJK, which is twice as wide. */
        const std::size_t width = length >= 3 ? 2 : 1;
        if (columns + width > kLineColumns) {
            if (used == kMaxRecordLines) {
                line += "...";
                break;
            }
            lines.push_back(line);
            line = "  ";
            columns = line.size();
            ++used;
        }
        line.append(value, offset, length);
        columns += width;
        offset += length;
    }
    lines.push_back(line);
}

/* The lines the Read and Tag pages show about one tag. */
std::vector<std::string> tag_lines(const model::NfcTag& tag) {
    std::vector<std::string> lines;
    lines.push_back(model::nfc_family_name(tag));
    lines.push_back("UID  " + model::nfc_hex(tag.uid.data(), tag.uid.size()));

    if (tag.technology == model::NfcTechnology::F) {
        lines.push_back("PMm  " + model::nfc_hex(tag.felica_pmm.data(), tag.felica_pmm.size()));
        if (tag.felica_lite) {
            lines.push_back(std::to_string(tag.blocks_read) + " of " + std::to_string(model::kFelicaLiteBlocks) +
                            " blocks read");
            /* S_PAD0 holds the first user data. */
            if (tag.data.size() >= model::kFelicaLiteRecord) {
                add_wrapped(lines, "Block 0", model::nfc_hex(tag.data.data() + 2, model::kFelicaBlockSize));
            }
            return lines;
        }
        for (const auto& system : tag.felica_systems) {
            std::size_t open = 0;
            for (uint16_t service : system.services) {
                open += model::felica_service_public(service) ? 1 : 0;
            }
            char code[8];
            std::snprintf(code, sizeof(code), "%04X", static_cast<unsigned>(system.code));
            lines.push_back(std::string("System ") + code + ": " + std::to_string(system.services.size()) +
                            " services (" + std::to_string(open) + " public), " +
                            std::to_string(system.blocks.size()) + " blocks read");
        }
        if (tag.felica_systems.empty()) {
            lines.push_back("No systems listed");
        }
        return lines;
    }

    if (tag.technology == model::NfcTechnology::B) {
        if (!tag.st25tb) {
            lines.push_back("App data  " + model::nfc_hex(tag.app_data.data(), tag.app_data.size()));
            lines.push_back("Protocol info  " + model::nfc_hex(tag.protocol_info.data(), tag.protocol_info.size()));
            /* Bit 0 of the second protocol byte: the card speaks ISO 14443-4. */
            lines.push_back((tag.protocol_info[1] & 0x01) != 0 ? "ISO 14443-4 card" : "Not an ISO 14443-4 card");
            return lines;
        }
        if (tag.block_count == 0) {
            lines.push_back("Unknown ST25TB product: memory not read");
            return lines;
        }
        lines.push_back(std::to_string(tag.blocks_read) + " of " + std::to_string(tag.block_count) +
                        " blocks of 4 bytes read");
        const std::size_t shown = std::min<std::size_t>(tag.data.size(), 16);
        add_wrapped(lines, "Data", model::nfc_hex(tag.data.data(), shown) + (tag.data.size() > shown ? " ..." : ""));
        lines.push_back("System block  " + model::nfc_hex(tag.st25tb_system.data(), tag.st25tb_system.size()));
        return lines;
    }

    if (tag.technology == model::NfcTechnology::V) {
        std::string info;
        if (tag.has_dsfid) {
            info += "DSFID " + model::nfc_hex(&tag.dsfid, 1) + "   ";
        }
        if (tag.has_afi) {
            info += "AFI " + model::nfc_hex(&tag.afi, 1) + "   ";
        }
        if (tag.has_ic_ref) {
            info += "IC ref " + model::nfc_hex(&tag.ic_ref, 1);
        }
        if (!info.empty()) {
            lines.push_back(info);
        }
        if (tag.block_count != 0) {
            std::size_t locked = 0;
            for (uint8_t status : tag.block_security) {
                locked += status != 0 ? 1 : 0;
            }
            lines.push_back(std::to_string(tag.blocks_read) + " of " + std::to_string(tag.block_count) +
                            " blocks of " + std::to_string(tag.block_size) + " bytes read" +
                            (locked != 0 ? ", " + std::to_string(locked) + " locked" : std::string()));
            /* The first bytes, as a hint of what the tag holds. */
            const std::size_t shown = std::min<std::size_t>(tag.data.size(), 16);
            add_wrapped(lines, "Data", model::nfc_hex(tag.data.data(), shown) + (tag.data.size() > shown ? " ..." : ""));
        }
        else {
            lines.push_back("No memory could be read");
        }
        return lines;
    }

    /* What the tag says comes before the technical details. */
    const model::NdefMessage ndef = model::nfc_read_ndef(tag);
    if (ndef.found) {
        for (const auto& record : ndef.records) {
            add_wrapped(lines, record.label, record.value);
        }
        if (!ndef.problem.empty()) {
            lines.push_back("NDEF  " + ndef.problem);
        }
        else if (ndef.records.empty()) {
            lines.push_back("NDEF  empty message");
        }
    }
    else if (tag.type2) {
        lines.push_back(ndef.formatted ? "NDEF  no message" : "NDEF  not formatted");
    }

    /* ATQA is shown high byte first, as readers print it. */
    const uint8_t atqa[2] = {tag.atqa[1], tag.atqa[0]};
    lines.push_back("ATQA " + model::nfc_hex(atqa, 2, "") + "   SAK " + model::nfc_hex(&tag.sak, 1));
    if (!tag.ats.empty()) {
        add_wrapped(lines, "ATS", model::nfc_hex(tag.ats.data(), tag.ats.size()));
    }
    if (tag.type2) {
        lines.push_back(std::to_string(tag.pages_read) + " of " + std::to_string(tag.pages_total) +
                        " pages read (" + std::to_string(tag.pages_total * model::kNfcPageSize) + " bytes)");
    }
    else {
        lines.push_back("Identity only: the memory is not read");
    }
    return lines;
}

} // namespace

lv_subject_t* BaseViewModel::nfc_revision_subject() {
    return nfc_revision_subject_.native();
}

lv_subject_t* BaseViewModel::nfc_message_subject() {
    return nfc_message_subject_.native();
}

lv_subject_t* BaseViewModel::nfc_selection_subject() {
    return nfc_saved_selection_subject_.native();
}

std::size_t BaseViewModel::nfc_list_count(model::AppPage page) const {
    switch (page) {
        case model::AppPage::NfcSaved:
            return nfc_files_.size();
        case model::AppPage::NfcCard:
            return nfc_card_lines_.size();
        default:
            return nfc_read_lines_.size();
    }
}

std::string BaseViewModel::nfc_list_text(model::AppPage page, std::size_t index) const {
    if (index >= nfc_list_count(page)) {
        return std::string();
    }
    switch (page) {
        case model::AppPage::NfcSaved:
            return nfc_files_[index].is_directory ? nfc_files_[index].name + "/" : nfc_files_[index].name;
        case model::AppPage::NfcCard:
            return nfc_card_lines_[index];
        default:
            return nfc_read_lines_[index];
    }
}

std::size_t BaseViewModel::nfc_selection(model::AppPage page) const {
    switch (page) {
        case model::AppPage::NfcSaved:
            return static_cast<std::size_t>(nfc_saved_selection_subject_.value());
        case model::AppPage::NfcCard:
            return nfc_card_line_;
        default:
            return nfc_read_line_;
    }
}

std::string BaseViewModel::nfc_list_summary(model::AppPage page) const {
    if (page == model::AppPage::NfcSaved) {
        const auto index = static_cast<std::size_t>(nfc_saved_selection_subject_.value());
        return index < nfc_files_.size() ? nfc_files_[index].summary : std::string();
    }
    return nfc_status_;
}

bool BaseViewModel::nfc_busy() const {
    return nfc_worker_ != nullptr;
}

void BaseViewModel::configure_nfc(const platform::St25r3916Config& config, const std::string& directory) {
    nfc_stop();
    nfc_config_ = config;
    nfc_directory_ = directory;
}

std::string BaseViewModel::nfc_directory() const {
    return nfc_directory_.empty() ? platform::default_nfc_directory() : nfc_directory_;
}

bool BaseViewModel::nfc_in_subfolder() const {
    return !nfc_path_.empty() && nfc_path_ != nfc_directory();
}

std::string BaseViewModel::nfc_browse_directory() const {
    return nfc_in_subfolder() ? nfc_path_ : nfc_directory();
}

void BaseViewModel::nfc_touch() {
    nfc_revision_subject_.set(nfc_revision_subject_.value() + 1);
}

/* ---- The worker and its poll timer ----------------------------------------- */

void BaseViewModel::nfc_start(platform::NfcWorker::Job job, const model::NfcTag& source) {
    nfc_stop();
    nfc_job_ = job;

    if (nfc_config_.spi_device.empty()) {
        nfc_status_ = "Set [nfc] spi_device in zerosub.conf";
        nfc_touch();
        return;
    }

    /* A Cap that has only just been powered needs a moment before the chip answers. */
    unsigned int settle_ms = 0;
    if (!cap_power_.enabled()) {
        std::string error;
        if (!cap_power_.enable(error)) {
            nfc_status_ = error;
            nfc_touch();
            return;
        }
        settle_ms = platform::CapPower::kSettleMs;
    }

    nfc_worker_ = platform::NfcWorker::start(nfc_config_, job, source, settle_ms);
    nfc_seen_revision_ = nfc_worker_->revision() - 1;
    if (!nfc_timer_) {
        nfc_timer_ = lv_timer_create(nfc_timer_cb, kNfcPollPeriodMs, this);
    }
    if (nfc_timer_) {
        lv_timer_resume(nfc_timer_);
    }
    poll_nfc();
}

void BaseViewModel::nfc_stop() {
    /* Joins the job's thread, which turns the field off on its way out. */
    nfc_worker_.reset();
    if (nfc_timer_) {
        lv_timer_pause(nfc_timer_);
    }
}

void BaseViewModel::poll_nfc() {
    if (!nfc_worker_) {
        if (nfc_timer_) {
            lv_timer_pause(nfc_timer_);
        }
        return;
    }
    const uint32_t revision = nfc_worker_->revision();
    if (revision == nfc_seen_revision_) {
        return;
    }
    nfc_seen_revision_ = revision;

    const bool running = nfc_worker_->running();
    auto status = nfc_worker_->status();
    using Phase = platform::NfcWorker::Phase;
    switch (nfc_job_) {
        case platform::NfcWorker::Job::Read:
            nfc_status_ = status.text;
            if (status.phase == Phase::Done) {
                nfc_read_tag_ = std::move(status.tag);
                nfc_read_valid_ = true;
                nfc_read_lines_ = tag_lines(nfc_read_tag_);
                nfc_read_line_ = 0;
                nfc_message_subject_.set("ENTER saves the tag, 5 reads another one");
            }
            break;
        case platform::NfcWorker::Job::Write:
            nfc_status_ = running ? "Writing: " + status.text : status.text;
            break;
        case platform::NfcWorker::Job::Emulate:
            nfc_status_ = running ? "Emulating: " + status.text : status.text;
            break;
    }

    if (!running) {
        /* The job has ended by itself; its last status line stays on the page. */
        nfc_stop();
    }
    nfc_touch();
}

void BaseViewModel::nfc_timer_cb(lv_timer_t* timer) {
    auto* view_model = static_cast<BaseViewModel*>(lv_timer_get_user_data(timer));
    if (view_model) {
        view_model->poll_nfc();
    }
}

void BaseViewModel::sync_nfc_activity() {
    nfc_delete_armed_ = false;
    switch (model_.current_page()) {
        case model::AppPage::NfcRead:
            nfc_read_again();
            break;
        case model::AppPage::NfcCard:
            /* Nothing runs until asked: emulating and writing both start with a key. */
            nfc_stop();
            nfc_status_ = card_idle_status(nfc_card_tag_);
            nfc_message_subject_.set(file_name_of(nfc_card_path_).c_str());
            nfc_touch();
            break;
        default:
            nfc_stop();
            break;
    }
}

/* ---- Read ------------------------------------------------------------------- */

void BaseViewModel::nfc_read_again() {
    if (model_.current_page() != model::AppPage::NfcRead) {
        return;
    }
    nfc_read_valid_ = false;
    nfc_read_lines_.clear();
    nfc_read_line_ = 0;
    nfc_message_subject_.set("");
    nfc_status_.clear();
    nfc_start(platform::NfcWorker::Job::Read, model::NfcTag{});
    nfc_touch();
}

/* ---- Saved Tags ------------------------------------------------------------- */

void BaseViewModel::nfc_refresh_files() {
    nfc_delete_armed_ = false;

    /* A folder outside the library (the card was swapped, the config changed) falls back to its root. */
    const std::string root = nfc_directory();
    if (nfc_path_.compare(0, root.size(), root) != 0) {
        nfc_path_.clear();
    }

    std::string error;
    /* Make sure the library exists, so there is a folder to copy tags into before the first save. */
    platform::ensure_capture_directory(root, error);
    if (platform::list_directory(nfc_browse_directory(), ".nfc", nfc_files_, error)) {
        nfc_message_subject_.set(nfc_browse_directory().c_str());
    }
    else {
        nfc_message_subject_.set(error.c_str());
    }

    const auto last = nfc_files_.empty() ? 0 : static_cast<int32_t>(nfc_files_.size()) - 1;
    nfc_saved_selection_subject_.set(std::clamp<int32_t>(nfc_saved_selection_subject_.value(), 0, last));
    nfc_describe_selection();
    nfc_touch();
}

void BaseViewModel::nfc_describe_selection() {
    /* Files are only read for the selected row, so large folders list quickly. */
    const auto index = static_cast<std::size_t>(nfc_saved_selection_subject_.value());
    if (index < nfc_files_.size() && nfc_files_[index].summary.empty()) {
        auto& entry = nfc_files_[index];
        entry.summary = entry.is_directory ? "Folder" : platform::describe_nfc_file(entry.path);
    }
}

bool BaseViewModel::nfc_up() {
    if (model_.current_page() != model::AppPage::NfcSaved || !nfc_in_subfolder()) {
        return false;
    }

    const std::filesystem::path current(nfc_path_);
    const std::string left_folder = current.filename().string();
    nfc_path_ = current.parent_path().string();
    nfc_saved_selection_subject_.set(0);
    nfc_refresh_files();

    /* Put the cursor back on the folder that was just left. */
    for (std::size_t index = 0; index < nfc_files_.size(); ++index) {
        if (nfc_files_[index].is_directory && nfc_files_[index].name == left_folder) {
            nfc_saved_selection_subject_.set(static_cast<int32_t>(index));
            nfc_describe_selection();
            nfc_touch();
            break;
        }
    }
    return true;
}

bool BaseViewModel::nfc_open_card(const std::string& path) {
    model::NfcTag tag;
    std::string error;
    if (!platform::load_nfc_file(path, tag, error)) {
        nfc_message_subject_.set((file_name_of(path) + ": " + error).c_str());
        return false;
    }
    nfc_card_path_ = path;
    nfc_card_original_ = tag;
    nfc_card_tag_ = std::move(tag);
    nfc_card_lines_ = tag_lines(nfc_card_tag_);
    nfc_card_line_ = 0;
    show_page(model::AppPage::NfcCard);
    return true;
}

void BaseViewModel::nfc_step_selection(int32_t step) {
    const model::AppPage page = model_.current_page();
    if (page == model::AppPage::NfcRead || page == model::AppPage::NfcCard) {
        /* These pages only scroll: the cursor walks through the tag's lines. */
        const std::size_t count = nfc_list_count(page);
        std::size_t& line = page == model::AppPage::NfcCard ? nfc_card_line_ : nfc_read_line_;
        if (step > 0 && line + 1 < count) {
            ++line;
        }
        else if (step < 0 && line > 0) {
            --line;
        }
        nfc_touch();
        return;
    }
    if (page != model::AppPage::NfcSaved || nfc_files_.empty()) {
        return;
    }
    nfc_delete_armed_ = false;
    const auto last = static_cast<int32_t>(nfc_files_.size()) - 1;
    nfc_saved_selection_subject_.set(std::clamp<int32_t>(nfc_saved_selection_subject_.value() + step, 0, last));
    nfc_describe_selection();
    nfc_message_subject_.set(nfc_browse_directory().c_str());
    nfc_touch();
}

void BaseViewModel::nfc_next() {
    nfc_step_selection(1);
}

void BaseViewModel::nfc_previous() {
    nfc_step_selection(-1);
}

void BaseViewModel::nfc_open() {
    if (model_.current_page() != model::AppPage::NfcSaved) {
        return;
    }
    const auto index = static_cast<std::size_t>(nfc_saved_selection_subject_.value());
    if (index >= nfc_files_.size()) {
        return;
    }
    nfc_delete_armed_ = false;
    if (nfc_files_[index].is_directory) {
        nfc_path_ = nfc_files_[index].path;
        nfc_saved_selection_subject_.set(0);
        nfc_refresh_files();
    }
    else {
        nfc_open_card(nfc_files_[index].path);
    }
}

/* ---- Keys ------------------------------------------------------------------- */

void BaseViewModel::nfc_activate() {
    switch (model_.current_page()) {
        case model::AppPage::NfcSaved:
            nfc_open();
            break;

        case model::AppPage::NfcRead: {
            if (!nfc_read_valid_) {
                break;
            }
            std::string path;
            std::string error;
            if (!platform::save_new_nfc_file(nfc_directory(), nfc_read_tag_, path, error)) {
                nfc_message_subject_.set(("Save failed: " + error).c_str());
                break;
            }
            /* The saved tag opens straight away, ready to emulate or write. */
            nfc_path_.clear();
            if (!nfc_open_card(path)) {
                break;
            }
            nfc_message_subject_.set(("Saved " + file_name_of(path)).c_str());
            break;
        }

        case model::AppPage::NfcCard:
            nfc_delete_armed_ = false;
            if (nfc_worker_) {
                nfc_stop();
                nfc_status_ = "Stopped";
            }
            else if (nfc_card_tag_.technology == model::NfcTechnology::V) {
                /* The ST25R3916 has no NFC-V card mode, and a host cannot meet NFC-V's timing. */
                nfc_status_ = "NFC-V tags cannot be emulated";
            }
            else if (nfc_card_tag_.technology == model::NfcTechnology::B) {
                /* The ST25R3916 answers as a card only in NFC-A and NFC-F. */
                nfc_status_ = "NFC-B tags cannot be emulated";
            }
            else if (nfc_card_tag_.technology == model::NfcTechnology::F) {
                nfc_status_ = "FeliCa emulation is not supported yet";
            }
            else {
                nfc_start(platform::NfcWorker::Job::Emulate, nfc_card_tag_);
            }
            nfc_message_subject_.set(file_name_of(nfc_card_path_).c_str());
            nfc_touch();
            break;

        default:
            break;
    }
}

void BaseViewModel::nfc_write() {
    if (model_.current_page() != model::AppPage::NfcCard) {
        return;
    }
    nfc_delete_armed_ = false;
    nfc_message_subject_.set(file_name_of(nfc_card_path_).c_str());
    if (nfc_worker_) {
        nfc_stop();
        nfc_status_ = "Stopped";
    }
    else if (!nfc_card_tag_.type2) {
        nfc_status_ = "Only NTAG / Ultralight tags can be written";
    }
    else {
        nfc_start(platform::NfcWorker::Job::Write, nfc_card_tag_);
    }
    nfc_touch();
}

void BaseViewModel::nfc_set_card_tag(model::NfcTag tag, const std::string& status) {
    /* A running emulation carries on with the new identity; a write is stopped. */
    const bool emulating = nfc_worker_ && nfc_job_ == platform::NfcWorker::Job::Emulate;
    nfc_stop();
    nfc_card_tag_ = std::move(tag);
    nfc_card_lines_ = tag_lines(nfc_card_tag_);
    nfc_card_line_ = std::min(nfc_card_line_, nfc_card_lines_.empty() ? 0 : nfc_card_lines_.size() - 1);
    if (emulating) {
        /* The status line then shows the emulation; the note goes on the line below. */
        nfc_start(platform::NfcWorker::Job::Emulate, nfc_card_tag_);
    }
    else {
        nfc_status_ = card_idle_status(nfc_card_tag_);
    }
    nfc_note(status);
}

void BaseViewModel::nfc_note(const std::string& text) {
    /* The line under the status: a running job never overwrites it. */
    nfc_message_subject_.set(text.c_str());
    nfc_touch();
}

bool BaseViewModel::nfc_card_is_amiibo() const {
    return model::amiibo_shaped(nfc_card_original_);
}

void BaseViewModel::nfc_new_uid() {
    if (model_.current_page() != model::AppPage::NfcCard) {
        return;
    }
    nfc_delete_armed_ = false;
    auto fail = [this](const std::string& text) { nfc_note(text); };
    if (!model::amiibo_shaped(nfc_card_original_)) {
        fail("A new UID needs an amiibo (NTAG215)");
        return;
    }

    std::vector<uint8_t> raw;
    model::AmiiboKeys keys;
    std::string error;
    if (!platform::load_amiibo_key_file(nfc_directory(), raw, error) || !model::amiibo_parse_keys(raw, keys, error)) {
        fail(error);
        return;
    }
    std::fill(raw.begin(), raw.end(), 0);

    std::random_device random;
    std::array<uint8_t, 7> uid{};
    uid[0] = 0x04;
    do {
        for (std::size_t index = 1; index < uid.size(); ++index) {
            uid[index] = static_cast<uint8_t>(random());
        }
    } while (std::equal(uid.begin(), uid.end(), nfc_card_tag_.uid.begin(), nfc_card_tag_.uid.end()) ||
             std::equal(uid.begin(), uid.end(), nfc_card_original_.uid.begin(), nfc_card_original_.uid.end()));
    std::array<uint8_t, 32> salt{};
    for (auto& byte : salt) {
        byte = static_cast<uint8_t>(random());
    }

    model::NfcTag tag;
    if (!model::amiibo_with_uid(nfc_card_original_, keys, uid, salt, tag, error)) {
        fail("New UID failed: " + error);
        return;
    }
    nfc_set_card_tag(std::move(tag), "New UID " + model::nfc_hex(uid.data(), uid.size()));
}

void BaseViewModel::nfc_original_uid() {
    if (model_.current_page() != model::AppPage::NfcCard) {
        return;
    }
    nfc_delete_armed_ = false;
    if (nfc_card_tag_.uid == nfc_card_original_.uid) {
        nfc_note("Already the file's own UID");
        return;
    }
    nfc_set_card_tag(nfc_card_original_, "Original UID restored");
}

bool BaseViewModel::nfc_stop_job() {
    if (model_.current_page() != model::AppPage::NfcCard || !nfc_worker_) {
        return false;
    }
    nfc_stop();
    nfc_status_ = "Stopped";
    nfc_touch();
    return true;
}

void BaseViewModel::nfc_delete() {
    const model::AppPage page = model_.current_page();
    std::string path;
    if (page == model::AppPage::NfcSaved) {
        const auto index = static_cast<std::size_t>(nfc_saved_selection_subject_.value());
        if (index >= nfc_files_.size()) {
            return;
        }
        if (nfc_files_[index].is_directory) {
            nfc_message_subject_.set("Folders cannot be deleted here");
            return;
        }
        path = nfc_files_[index].path;
    }
    else if (page == model::AppPage::NfcCard && !nfc_worker_) {
        path = nfc_card_path_;
    }
    else {
        return;
    }

    if (!nfc_delete_armed_) {
        nfc_delete_armed_ = true;
        nfc_message_subject_.set(("Press 8 again to delete " + file_name_of(path)).c_str());
        return;
    }
    nfc_delete_armed_ = false;

    std::string error;
    if (!platform::delete_nfc_file(path, error)) {
        nfc_message_subject_.set(("Delete failed: " + error).c_str());
        return;
    }
    const std::string deleted = "Deleted " + file_name_of(path);
    if (page == model::AppPage::NfcCard) {
        show_page(model::AppPage::NfcSaved);
    }
    else {
        nfc_refresh_files();
    }
    nfc_message_subject_.set(deleted.c_str());
}

} // namespace viewmodel
