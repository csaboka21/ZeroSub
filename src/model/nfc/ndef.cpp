/*
 * SPDX-License-Identifier: MIT
 *
 * Type 2 tag layout: page 3 is the capability container (E1, version, data
 * area size / 8, access). The data area starts at page 4 and holds TLV
 * blocks; the one with tag 03 is the NDEF message.
 *
 * An NDEF record: a flags byte (message begin / end, chunked, short record,
 * has ID, and a 3-bit type name format), type length, payload length (one
 * byte for a short record, else four), optional ID length, then type, ID
 * and payload.
 */

#include "ndef.h"

#include <algorithm>
#include <array>
#include <utility>

namespace model {
namespace {

constexpr std::size_t kCapabilityOffset = 3 * kNfcPageSize;
constexpr std::size_t kDataOffset = 4 * kNfcPageSize;
constexpr uint8_t kCapabilityMagic = 0xE1;

constexpr uint8_t kTlvNull = 0x00;
constexpr uint8_t kTlvNdef = 0x03;
constexpr uint8_t kTlvTerminator = 0xFE;

constexpr uint8_t kFlagMessageEnd = 0x40;
constexpr uint8_t kFlagChunked = 0x20;
constexpr uint8_t kFlagShort = 0x10;
constexpr uint8_t kFlagHasId = 0x08;
constexpr uint8_t kTnfMask = 0x07;

constexpr uint8_t kTnfEmpty = 0;
constexpr uint8_t kTnfWellKnown = 1;
constexpr uint8_t kTnfMedia = 2;
constexpr uint8_t kTnfAbsoluteUri = 3;
constexpr uint8_t kTnfExternal = 4;

/* Longest value kept for one record; a tag holds less than this anyway. */
constexpr std::size_t kMaxValueBytes = 900;
constexpr std::size_t kMaxRecords = 16;
/* A Smart Poster holds a message of its own; deeper nesting is not followed. */
constexpr int kMaxDepth = 2;

/* URI record prefixes, by the code in the first payload byte (NFC Forum URI RTD). */
constexpr std::array<const char*, 36> kUriPrefixes = {
    "",
    "http://www.",
    "https://www.",
    "http://",
    "https://",
    "tel:",
    "mailto:",
    "ftp://anonymous:anonymous@",
    "ftp://ftp.",
    "ftps://",
    "sftp://",
    "smb://",
    "nfs://",
    "ftp://",
    "dav://",
    "news:",
    "telnet://",
    "imap:",
    "rtsp://",
    "sip:",
    "sips:",
    "tftp:",
    "btspp://",
    "btl2cap://",
    "btgoep://",
    "tcpobex://",
    "irdaobex://",
    "file://",
    "urn:epc:id:",
    "urn:epc:tag:",
    "urn:epc:pat:",
    "urn:epc:raw:",
    "urn:epc:",
    "urn:nfc:",
    "",
    "",
};

bool starts_with(const std::string& text, const char* prefix) {
    return text.compare(0, std::char_traits<char>::length(prefix), prefix) == 0;
}

/* Text for one display line: control characters become spaces, and it is cut at a sane length. */
std::string clean(const uint8_t* bytes, std::size_t size) {
    std::string text;
    size = std::min(size, kMaxValueBytes);
    text.reserve(size);
    for (std::size_t index = 0; index < size; ++index) {
        const uint8_t byte = bytes[index];
        text.push_back(byte < 0x20 || byte == 0x7F ? ' ' : static_cast<char>(byte));
    }
    /* Do not leave half a UTF-8 character at the cut. */
    if (size == kMaxValueBytes) {
        while (!text.empty() && (static_cast<uint8_t>(text.back()) & 0xC0) == 0x80) {
            text.pop_back();
        }
        if (!text.empty() && (static_cast<uint8_t>(text.back()) & 0x80) != 0) {
            text.pop_back();
        }
    }
    while (!text.empty() && text.back() == ' ') {
        text.pop_back();
    }
    return text;
}

std::string clean(const std::string& text) {
    return clean(reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

void append_utf8(std::string& text, uint32_t code_point) {
    if (code_point < 0x80) {
        text.push_back(static_cast<char>(code_point));
    }
    else if (code_point < 0x800) {
        text.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        text.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
    else if (code_point < 0x10000) {
        text.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        text.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
    else {
        text.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
        text.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
}

/* UTF-16 as a Text record may carry it: big-endian unless a byte order mark says otherwise. */
std::string utf16_to_utf8(const uint8_t* bytes, std::size_t size) {
    bool little_endian = false;
    std::size_t offset = 0;
    if (size >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        little_endian = true;
        offset = 2;
    }
    else if (size >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) {
        offset = 2;
    }

    const auto unit_at = [&](std::size_t position) {
        return static_cast<uint32_t>(little_endian ? bytes[position] | (bytes[position + 1] << 8)
                                                   : (bytes[position] << 8) | bytes[position + 1]);
    };

    std::string text;
    while (offset + 1 < size) {
        uint32_t code_point = unit_at(offset);
        offset += 2;
        if (code_point >= 0xD800 && code_point < 0xDC00 && offset + 1 < size) {
            const uint32_t low = unit_at(offset);
            if (low >= 0xDC00 && low < 0xE000) {
                code_point = 0x10000 + ((code_point - 0xD800) << 10) + (low - 0xDC00);
                offset += 2;
            }
        }
        append_utf8(text, code_point);
    }
    return text;
}

NdefRecord uri_record(const std::string& uri) {
    const char* label = "URI";
    if (starts_with(uri, "http://") || starts_with(uri, "https://")) {
        label = "URL";
    }
    else if (starts_with(uri, "tel:")) {
        label = "Phone";
    }
    else if (starts_with(uri, "mailto:")) {
        label = "Email";
    }
    else if (starts_with(uri, "sms:")) {
        label = "SMS";
    }
    else if (starts_with(uri, "geo:")) {
        label = "Location";
    }
    return {label, clean(uri)};
}

/* The value of one "KEY:value" line of a vCard, or empty. */
std::string vcard_field(const std::string& card, const char* key) {
    const std::string wanted = std::string(key);
    std::size_t start = 0;
    while (start < card.size()) {
        std::size_t end = card.find('\n', start);
        if (end == std::string::npos) {
            end = card.size();
        }
        const std::string line = card.substr(start, end - start);
        /* "FN:Name" or "FN;CHARSET=UTF-8:Name". */
        if (line.compare(0, wanted.size(), wanted) == 0 && line.size() > wanted.size() &&
            (line[wanted.size()] == ':' || line[wanted.size()] == ';')) {
            const std::size_t colon = line.find(':');
            if (colon != std::string::npos) {
                return clean(line.substr(colon + 1));
            }
        }
        start = end + 1;
    }
    return std::string();
}

/* The network name in a Wi-Fi Simple Configuration record; the password in it is not shown. */
std::string wifi_ssid(const uint8_t* bytes, std::size_t size, int depth) {
    constexpr uint16_t kCredential = 0x100E;
    constexpr uint16_t kSsid = 0x1045;
    std::size_t offset = 0;
    while (offset + 4 <= size) {
        const uint16_t type = static_cast<uint16_t>((bytes[offset] << 8) | bytes[offset + 1]);
        const std::size_t length = static_cast<std::size_t>((bytes[offset + 2] << 8) | bytes[offset + 3]);
        offset += 4;
        if (length > size - offset) {
            break;
        }
        if (type == kSsid) {
            return clean(bytes + offset, length);
        }
        if (type == kCredential && depth < 2) {
            const std::string nested = wifi_ssid(bytes + offset, length, depth + 1);
            if (!nested.empty()) {
                return nested;
            }
        }
        offset += length;
    }
    return std::string();
}

std::string size_text(std::size_t size) {
    return std::to_string(size) + (size == 1 ? " byte" : " bytes");
}

std::vector<NdefRecord> parse_records(const uint8_t* bytes, std::size_t size, std::string& problem, int depth);

void describe_record(uint8_t tnf,
                     const std::string& type,
                     const uint8_t* payload,
                     std::size_t size,
                     int depth,
                     std::vector<NdefRecord>& records) {
    if (tnf == kTnfEmpty) {
        return;
    }

    if (tnf == kTnfWellKnown && type == "U") {
        if (size == 0) {
            return;
        }
        std::string uri = payload[0] < kUriPrefixes.size() ? kUriPrefixes[payload[0]] : "";
        uri.append(reinterpret_cast<const char*>(payload + 1), size - 1);
        records.push_back(uri_record(uri));
        return;
    }

    if (tnf == kTnfWellKnown && type == "T") {
        if (size == 0) {
            return;
        }
        const bool utf16 = (payload[0] & 0x80) != 0;
        const std::size_t language_size = std::min<std::size_t>(payload[0] & 0x3F, size - 1);
        const std::string language = clean(payload + 1, language_size);
        const uint8_t* text = payload + 1 + language_size;
        const std::size_t text_size = size - 1 - language_size;
        records.push_back({language.empty() ? "Text" : "Text (" + language + ")",
                           utf16 ? clean(utf16_to_utf8(text, text_size)) : clean(text, text_size)});
        return;
    }

    if (tnf == kTnfWellKnown && type == "Sp" && depth < kMaxDepth) {
        /* A Smart Poster is a message of its own: usually a URI and a title. */
        std::string ignored;
        auto nested = parse_records(payload, size, ignored, depth + 1);
        for (auto& record : nested) {
            if (starts_with(record.label, "Text")) {
                record.label = "Title" + record.label.substr(4);
            }
            records.push_back(std::move(record));
        }
        if (nested.empty()) {
            records.push_back({"Smart Poster", "empty"});
        }
        return;
    }

    if (tnf == kTnfAbsoluteUri) {
        /* The URI is the type field itself. */
        records.push_back(uri_record(type));
        return;
    }

    if (tnf == kTnfMedia) {
        if (type == "text/plain") {
            records.push_back({"Text", clean(payload, size)});
        }
        else if (type == "text/vcard" || type == "text/x-vcard" || type == "text/x-vCard") {
            const std::string card(reinterpret_cast<const char*>(payload), size);
            std::string name = vcard_field(card, "FN");
            if (name.empty()) {
                name = vcard_field(card, "N");
            }
            records.push_back({"Contact", name.empty() ? size_text(size) : name});
            const std::string phone = vcard_field(card, "TEL");
            if (!phone.empty()) {
                records.push_back({"Phone", phone});
            }
            const std::string email = vcard_field(card, "EMAIL");
            if (!email.empty()) {
                records.push_back({"Email", email});
            }
        }
        else if (type == "application/vnd.wfa.wsc") {
            const std::string ssid = wifi_ssid(payload, size, 0);
            records.push_back({"Wi-Fi", ssid.empty() ? "network settings" : ssid});
        }
        else if (type == "application/vnd.bluetooth.ep.oob" || type == "application/vnd.bluetooth.le.oob") {
            records.push_back({"Bluetooth", "pairing data, " + size_text(size)});
        }
        else {
            records.push_back({clean(type), size_text(size)});
        }
        return;
    }

    if (tnf == kTnfExternal && type == "android.com:pkg") {
        records.push_back({"Android app", clean(payload, size)});
        return;
    }

    /* Anything else is named, not interpreted. */
    std::string label = clean(type);
    if (label.empty()) {
        label = "Record";
    }
    records.push_back({label, size_text(size)});
}

std::vector<NdefRecord> parse_records(const uint8_t* bytes, std::size_t size, std::string& problem, int depth) {
    std::vector<NdefRecord> records;
    problem.clear();
    std::size_t offset = 0;
    while (offset < size && records.size() < kMaxRecords) {
        const uint8_t flags = bytes[offset++];
        const bool short_record = (flags & kFlagShort) != 0;
        const bool has_id = (flags & kFlagHasId) != 0;
        const std::size_t header = 1 + (short_record ? 1 : 4) + (has_id ? 1 : 0);
        if (size - offset < header) {
            problem = "cut off";
            break;
        }

        const std::size_t type_size = bytes[offset++];
        std::size_t payload_size = 0;
        if (short_record) {
            payload_size = bytes[offset++];
        }
        else {
            payload_size = (static_cast<std::size_t>(bytes[offset]) << 24) |
                           (static_cast<std::size_t>(bytes[offset + 1]) << 16) |
                           (static_cast<std::size_t>(bytes[offset + 2]) << 8) | bytes[offset + 3];
            offset += 4;
        }
        const std::size_t id_size = has_id ? bytes[offset++] : 0;

        const std::size_t remaining = size - offset;
        if (type_size > remaining || id_size > remaining - type_size ||
            payload_size > remaining - type_size - id_size) {
            problem = "cut off";
            break;
        }
        const std::string type(reinterpret_cast<const char*>(bytes + offset), type_size);
        offset += type_size + id_size;
        const uint8_t* payload = bytes + offset;
        offset += payload_size;

        if ((flags & kFlagChunked) != 0) {
            /* A payload split over several records; rare, and not put back together here. */
            problem = "chunked records are not shown";
            break;
        }
        describe_record(static_cast<uint8_t>(flags & kTnfMask), type, payload, payload_size, depth, records);
        if ((flags & kFlagMessageEnd) != 0) {
            break;
        }
    }
    return records;
}

} // namespace

std::vector<NdefRecord> ndef_parse_records(const uint8_t* bytes, std::size_t size, std::string& problem) {
    if (!bytes || size == 0) {
        problem.clear();
        return {};
    }
    return parse_records(bytes, size, problem, 0);
}

NdefMessage nfc_read_ndef(const NfcTag& tag) {
    NdefMessage message;
    if (!tag.type2 || tag.data.size() < kDataOffset || tag.pages_read < 4) {
        return message;
    }
    const uint8_t* capability = tag.data.data() + kCapabilityOffset;
    if (capability[0] != kCapabilityMagic) {
        return message;
    }
    message.formatted = true;

    /* The data area as the tag declares it, but never past what was read. */
    const std::size_t known = std::min<std::size_t>(tag.data.size(), static_cast<std::size_t>(tag.pages_read) * kNfcPageSize);
    const std::size_t end = std::min(known, kDataOffset + static_cast<std::size_t>(capability[2]) * 8);
    const uint8_t* data = tag.data.data();

    std::size_t offset = kDataOffset;
    while (offset < end) {
        const uint8_t block = data[offset++];
        if (block == kTlvNull) {
            continue;
        }
        if (block == kTlvTerminator || offset >= end) {
            break;
        }
        std::size_t length = data[offset++];
        if (length == 0xFF) {
            if (end - offset < 2) {
                break;
            }
            length = (static_cast<std::size_t>(data[offset]) << 8) | data[offset + 1];
            offset += 2;
        }
        if (block != kTlvNdef) {
            /* Lock control, memory control and proprietary blocks are skipped. */
            offset += std::min(length, end - offset);
            continue;
        }

        message.found = true;
        const std::size_t available = std::min(length, end - offset);
        message.records = ndef_parse_records(data + offset, available, message.problem);
        if (available < length && message.problem.empty()) {
            message.problem = "cut off";
        }
        break;
    }
    return message;
}

} // namespace model
