/*
 * SPDX-License-Identifier: MIT
 *
 * BaseViewModel: the Add Manually page, which creates a new fixed-code remote.
 */

#include "base_viewmodel.h"

#include "protocols/key_factory.h"
#include "transmit_policy.h"

#include <cstdio>
#include <filesystem>

namespace viewmodel {
namespace {

std::size_t digit_count(const model::KeyTemplate& key_template) {
    return (key_template.bits + 3) / 4;
}

} // namespace

lv_subject_t* BaseViewModel::add_revision_subject() {
    return add_revision_subject_.native();
}

lv_subject_t* BaseViewModel::add_message_subject() {
    return add_message_subject_.native();
}

BaseViewModel::AddField BaseViewModel::add_field() const {
    return add_field_;
}

std::string BaseViewModel::add_field_value(AddField field) const {
    const auto& key_template = model::key_template(add_protocol_);
    char text[64];
    switch (field) {
        case AddField::Protocol:
            std::snprintf(text, sizeof(text), "%s %u bit", key_template.protocol,
                          static_cast<unsigned>(key_template.bits));
            return text;
        case AddField::Frequency: {
            const uint32_t hz = model::BaseModel::frequency_hz_at(add_frequency_);
            std::snprintf(text, sizeof(text), "%u.%02u MHz", static_cast<unsigned>(hz / 1000000U),
                          static_cast<unsigned>((hz % 1000000U) / 10000U));
            return text;
        }
        case AddField::Modulation:
            return platform::cc1101_preset_name(static_cast<platform::Cc1101Preset>(add_preset_));
        case AddField::Key: {
            /* Hex digits, most significant first; the digit being edited is bracketed. */
            const std::size_t digits = digit_count(key_template);
            std::string value;
            for (std::size_t digit = 0; digit < digits; ++digit) {
                const unsigned nibble =
                    static_cast<unsigned>((add_key_ >> (4 * (digits - 1 - digit))) & 0xFU);
                const char character = "0123456789ABCDEF"[nibble];
                if (add_field_ == AddField::Key && digit == add_cursor_) {
                    value += '[';
                    value += character;
                    value += ']';
                }
                else {
                    value += character;
                }
            }
            return value;
        }
    }
    return std::string();
}

void BaseViewModel::add_prepare() {
    /* The first visit starts with a fresh random key; later visits keep what was being edited. */
    if (!add_initialized_) {
        add_key_ = model::random_key(model::key_template(add_protocol_));
        add_initialized_ = true;
    }
    add_publish("");
}

void BaseViewModel::add_publish(const std::string& message) {
    const auto& key_template = model::key_template(add_protocol_);
    add_key_ = model::normalize_key(key_template, add_key_);
    const std::size_t digits = digit_count(key_template);
    if (add_cursor_ >= digits) {
        add_cursor_ = digits - 1;
    }

    std::string text = message;
    if (text.empty()) {
        /* Say up front when the chosen frequency can be saved but never sent from here. */
        std::string reason;
        if (!model::transmit_allowed(model::BaseModel::frequency_hz_at(add_frequency_), reason)) {
            text = "Outside the EU transmit bands: can be saved, not sent";
        }
    }
    add_message_subject_.set(text.c_str());
    add_revision_subject_.set(add_revision_subject_.value() + 1);
}

void BaseViewModel::add_field_next() {
    add_field_ = static_cast<AddField>((static_cast<int>(add_field_) + 1) % kAddFieldCount);
    add_publish("");
}

void BaseViewModel::add_field_previous() {
    add_field_ = static_cast<AddField>((static_cast<int>(add_field_) + kAddFieldCount - 1) % kAddFieldCount);
    add_publish("");
}

void BaseViewModel::add_step_value(bool forward) {
    switch (add_field_) {
        case AddField::Protocol: {
            const std::size_t count = model::key_template_count();
            add_protocol_ = (add_protocol_ + (forward ? 1 : count - 1)) % count;
            /* A key made for one protocol means nothing in another: start a new one. */
            add_key_ = model::random_key(model::key_template(add_protocol_));
            add_cursor_ = 0;
            break;
        }
        case AddField::Frequency:
            add_frequency_ = (add_frequency_ + (forward ? 1 : model::kFrequencyCount - 1)) %
                             model::kFrequencyCount;
            break;
        case AddField::Modulation:
            add_preset_ = (add_preset_ + (forward ? 1 : model::kPresetCount - 1)) % model::kPresetCount;
            break;
        case AddField::Key: {
            const std::size_t digits = digit_count(model::key_template(add_protocol_));
            add_cursor_ = (add_cursor_ + (forward ? 1 : digits - 1)) % digits;
            break;
        }
    }
    add_publish("");
}

void BaseViewModel::add_value_next() {
    add_step_value(true);
}

void BaseViewModel::add_value_previous() {
    add_step_value(false);
}

void BaseViewModel::add_digit_step(bool up) {
    if (add_field_ != AddField::Key) {
        return;
    }
    const std::size_t digits = digit_count(model::key_template(add_protocol_));
    const unsigned shift = static_cast<unsigned>(4 * (digits - 1 - add_cursor_));
    const uint64_t nibble = (add_key_ >> shift) & 0xFU;
    const uint64_t changed = (nibble + (up ? 1 : 15)) & 0xFU;
    add_key_ = (add_key_ & ~(0xFULL << shift)) | (changed << shift);
    /* Publishing puts back any bits the protocol fixes, so the key shown is always a valid one. */
    add_publish("");
}

void BaseViewModel::add_randomize() {
    add_key_ = model::random_key(model::key_template(add_protocol_));
    add_publish("");
}

void BaseViewModel::add_save() {
    const auto& key_template = model::key_template(add_protocol_);
    const auto preset = static_cast<platform::Cc1101Preset>(add_preset_);

    platform::KeyCapture capture;
    capture.frequency_hz = model::BaseModel::frequency_hz_at(add_frequency_);
    capture.preset = platform::cc1101_preset_sub_file_name(preset);
    capture.protocol = key_template.protocol;
    capture.bits = key_template.bits;
    capture.key = model::normalize_key(key_template, add_key_);
    capture.te_us = key_template.te_us;

    std::string path;
    std::string error;
    if (!platform::save_key_capture(capture_directory(), capture, path, error)) {
        add_publish("Save failed: " + error);
        return;
    }

    /* The next visit to this page starts a new remote rather than repeating this one. */
    add_initialized_ = false;
    /* Go straight to the new file's summary, where ENTER sends it for pairing. */
    saved_path_.clear();
    saved_selection_subject_.set(0);
    if (!show_file_info(path, error)) {
        add_publish("Saved " + std::filesystem::path(path).filename().string());
    }
}

} // namespace viewmodel
