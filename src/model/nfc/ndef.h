/*
 * SPDX-License-Identifier: MIT
 *
 * Reads the NDEF message of an NTAG / Ultralight tag (NFC Forum Type 2): the
 * URL, text, contact or other records a phone would act on. Pure logic.
 */

#pragma once

#include "nfc_tag.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace model {

/* One record, ready to show: "URL" / "https://example.com", "Text (en)" / "Hello". */
struct NdefRecord {
    std::string label;
    std::string value;
};

struct NdefMessage {
    /* The tag has a capability container, so it is set up to hold NDEF. */
    bool formatted{false};
    /* An NDEF message was found; it may still be empty. */
    bool found{false};
    std::vector<NdefRecord> records;
    /* Set when the message could not be read to its end, e.g. "cut off". */
    std::string problem;
};

/* The NDEF message in the tag's memory. Tags that are not NTAG / Ultralight give an unformatted result. */
NdefMessage nfc_read_ndef(const NfcTag& tag);

/* The records of one raw NDEF message. `problem` is set when it is damaged or cut off. */
std::vector<NdefRecord> ndef_parse_records(const uint8_t* bytes, std::size_t size, std::string& problem);

} // namespace model
