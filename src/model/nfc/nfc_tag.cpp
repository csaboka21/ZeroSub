/*
 * SPDX-License-Identifier: MIT
 */

#include "nfc_tag.h"

#include "des.h"
#include "iso15693.h"

#include <algorithm>
#include <cstdio>
#include <random>
#include <utility>

namespace model {
namespace {

/* GET_VERSION product types. */
constexpr uint8_t kProductUltralight = 0x03;
constexpr uint8_t kProductNtag = 0x04;

/*
 * Page counts and user memory ends from the NXP data sheets. The pages from
 * user_end on are, depending on the type: dynamic lock bytes, configuration,
 * password and its acknowledge, or (NTAG203) the counter.
 */
constexpr NfcType2Spec kSpecs[] = {
    {"Mifare Ultralight", 0, 0, 16, 16},
    {"NTAG203", 0, 0, 42, 40},
    /* Pages 40-43 lock bytes, counter, AUTH0, AUTH1; 44-47 the 3DES key, never readable. */
    {"Mifare Ultralight C", 0, 0, 48, 40},
    {"Mifare Ultralight 11", kProductUltralight, 0x0B, 20, 16},
    {"Mifare Ultralight 21", kProductUltralight, 0x0E, 41, 36},
    {"NTAG210", kProductNtag, 0x0B, 20, 16},
    {"NTAG212", kProductNtag, 0x0E, 41, 36},
    {"NTAG213", kProductNtag, 0x0F, 45, 40},
    {"NTAG215", kProductNtag, 0x11, 135, 130},
    {"NTAG216", kProductNtag, 0x13, 231, 226},
};

constexpr uint8_t kCmdRead = 0x30;
constexpr uint8_t kCmdFastRead = 0x3A;
constexpr uint8_t kCmdWrite = 0xA2;
constexpr uint8_t kCmdGetVersion = 0x60;
constexpr uint8_t kCmdReadSignature = 0x3C;
constexpr uint8_t kCmdReadCounter = 0x39;
constexpr uint8_t kCmdCheckTearing = 0x3E;
constexpr uint8_t kCmdPasswordAuth = 0x1B;
constexpr uint8_t kCmdIncrementCounter = 0xA5;
constexpr uint8_t kCmdUlcAuthenticate = 0x1A;
constexpr uint8_t kUlcAuthContinue = 0xAF;
constexpr uint8_t kCmdHalt = 0x50;

/* ACCESS byte (first byte of the second configuration page) of NTAG21x. */
constexpr uint8_t kAccessNfcCounterEnabled = 0x10;
constexpr uint8_t kAccessNfcCounterPasswordProtected = 0x08;
constexpr uint32_t kCounterMax = 0xFFFFFF;

/* Ultralight C: where the key and the protection settings live. */
constexpr std::size_t kUlcKeyPage = 44;
constexpr std::size_t kUlcAuth0Page = 42;
constexpr std::size_t kUlcAuth1Page = 43;
constexpr uint16_t kUlcPages = 48;

constexpr uint8_t kAck = 0x0A;
/* Refusal codes of the NTAG21x: invalid argument, and anything else. */
constexpr uint8_t kNakArgument = 0x00;
constexpr uint8_t kNakOther = 0x01;

constexpr uint16_t kFirstUserPage = 4;
constexpr uint8_t kCascadeTag = 0x88;

} // namespace

const NfcType2Spec* nfc_type2_spec_for_version(const std::array<uint8_t, 8>& version) {
    for (const auto& spec : kSpecs) {
        if (spec.product_type != 0 && spec.product_type == version[2] && spec.storage_size == version[6]) {
            return &spec;
        }
    }
    return nullptr;
}

const NfcType2Spec* nfc_type2_spec_by_name(const std::string& name) {
    for (const auto& spec : kSpecs) {
        if (name == spec.name) {
            return &spec;
        }
    }
    return nullptr;
}

const char* nfc_type2_name_for_pages(uint16_t pages_total) {
    if (pages_total == 42) {
        return "NTAG203";
    }
    return pages_total == kUlcPages ? "Mifare Ultralight C" : "Mifare Ultralight";
}

bool nfc_is_ultralight_c(const NfcTag& tag) {
    return tag.type2 && tag.pages_total == kUlcPages && tag.type_name == "Mifare Ultralight C";
}

uint16_t nfc_user_end(const NfcTag& tag) {
    if (const auto* spec = nfc_type2_spec_by_name(tag.type_name)) {
        if (spec->pages_total == tag.pages_total) {
            return spec->user_end;
        }
    }
    return tag.pages_total;
}

namespace {

/* As Flipper Zero tells them apart: bits 7-2 of the third UID byte. */
constexpr St25tbSpec kSt25tbSpecs[] = {
    {"512AT", "ST25TB512-AT / SRI512", 16},
    {"512AC", "ST25TB512-AC / SRT512", 16},
    {"X512", "SRIX512", 16},
    {"2K", "ST25TB02K / SRI2K", 64},
    {"4K", "ST25TB04K / SRI4K", 128},
    {"X4K", "SRIX4K", 128},
};

} // namespace

const St25tbSpec* nfc_st25tb_spec_for_uid(const std::vector<uint8_t>& uid) {
    if (uid.size() != 8) {
        return nullptr;
    }
    switch (uid[2] >> 2) {
        case 0x0:
        case 0x3:
            return &kSt25tbSpecs[5];
        case 0x4:
            return &kSt25tbSpecs[2];
        case 0x6:
            return &kSt25tbSpecs[1];
        case 0x7:
            return &kSt25tbSpecs[4];
        case 0xC:
            return &kSt25tbSpecs[0];
        case 0xF:
            return &kSt25tbSpecs[3];
        default:
            return nullptr;
    }
}

const St25tbSpec* nfc_st25tb_spec_by_name(const std::string& name) {
    for (const auto& spec : kSt25tbSpecs) {
        if (name == spec.type_name) {
            return &spec;
        }
    }
    return nullptr;
}

std::string nfc_family_name(const NfcTag& tag) {
    if (tag.technology == NfcTechnology::V) {
        return iso15693_type_name(tag.uid);
    }
    if (tag.technology == NfcTechnology::F) {
        return felica_ic_name(tag.felica_pmm);
    }
    if (tag.technology == NfcTechnology::B) {
        if (tag.st25tb) {
            const St25tbSpec* spec = nfc_st25tb_spec_by_name(tag.st25tb_type);
            return spec ? spec->full_name : "ST25TB";
        }
        return "ISO 14443-B card";
    }
    if (tag.type2) {
        return tag.type_name.empty() ? "NTAG / Ultralight" : tag.type_name;
    }
    switch (tag.sak) {
        case 0x00:
            return "NFC-A Type 2 tag";
        case 0x08:
        case 0x88:
            return "MIFARE Classic 1K";
        case 0x09:
            return "MIFARE Classic Mini";
        case 0x18:
            return "MIFARE Classic 4K";
        case 0x10:
            return "MIFARE Plus 2K";
        case 0x11:
            return "MIFARE Plus 4K";
        case 0x28:
        case 0x38:
            return "MIFARE Classic + ISO 14443-4";
        default:
            break;
    }
    return (tag.sak & 0x20) != 0 ? "ISO 14443-4 card" : "NFC-A tag";
}

std::string nfc_hex(const uint8_t* bytes, std::size_t size, const char* separator) {
    std::string text;
    char byte_text[4];
    for (std::size_t index = 0; index < size; ++index) {
        if (index != 0) {
            text += separator;
        }
        std::snprintf(byte_text, sizeof(byte_text), "%02X", static_cast<unsigned>(bytes[index]));
        text += byte_text;
    }
    return text;
}

uint16_t nfc_crc_a(const uint8_t* bytes, std::size_t size) {
    uint16_t crc = 0x6363;
    for (std::size_t index = 0; index < size; ++index) {
        uint8_t value = static_cast<uint8_t>(bytes[index] ^ (crc & 0xFF));
        value = static_cast<uint8_t>(value ^ (value << 4));
        crc = static_cast<uint16_t>((crc >> 8) ^ (static_cast<uint16_t>(value) << 8) ^
                                    (static_cast<uint16_t>(value) << 3) ^ (value >> 4));
    }
    return crc;
}

uint8_t nfc_bcc(const uint8_t* bytes) {
    return static_cast<uint8_t>(bytes[0] ^ bytes[1] ^ bytes[2] ^ bytes[3]);
}

std::size_t nfc_cascade_levels(const std::vector<uint8_t>& uid, std::array<std::array<uint8_t, 5>, 3>& levels) {
    const std::size_t size = uid.size();
    if (size != 4 && size != 7 && size != 10) {
        return 0;
    }
    const std::size_t count = (size - 1) / 3;
    std::size_t offset = 0;
    for (std::size_t level = 0; level < count; ++level) {
        auto& group = levels[level];
        if (level + 1 < count) {
            /* Not the last level: a cascade tag, then three UID bytes. */
            group[0] = kCascadeTag;
            std::copy_n(uid.begin() + static_cast<std::ptrdiff_t>(offset), 3, group.begin() + 1);
            offset += 3;
        }
        else {
            std::copy_n(uid.begin() + static_cast<std::ptrdiff_t>(offset), 4, group.begin());
            offset += 4;
        }
        group[4] = nfc_bcc(group.data());
    }
    return count;
}

NfcType2Emulator::NfcType2Emulator(NfcTag tag) : tag_(std::move(tag)) {
    tag_.data.resize(static_cast<std::size_t>(tag_.pages_total) * kNfcPageSize, 0);

    /*
     * A file can name an NTAG21x / Ultralight EV1 without holding its
     * GET_VERSION answer (hand-made or older files). Readers, phones above
     * all, ask for it first and give up on a tag that refuses, so the answer
     * the real product gives is made up from the type.
     */
    if (!tag_.has_version) {
        const NfcType2Spec* spec = nfc_type2_spec_by_name(tag_.type_name);
        if (spec && spec->product_type != 0 && spec->pages_total == tag_.pages_total) {
            /* Vendor NXP; subtype 02 for the NTAG213/215/216, 01 for the others. */
            const bool ntag21x = spec->product_type == kProductNtag && spec->storage_size >= 0x0F;
            tag_.version = {0x00, 0x04, spec->product_type, static_cast<uint8_t>(ntag21x ? 0x02 : 0x01),
                            0x01, 0x00, spec->storage_size, 0x03};
            tag_.has_version = true;
        }
    }

    if (nfc_is_ultralight_c(tag_)) {
        kind_ = Kind::UltralightC;
    }
    else if (tag_.has_version && tag_.version[2] == kProductNtag && tag_.version[6] >= 0x0F) {
        kind_ = Kind::Ntag21x;
    }
    else if (tag_.has_version && tag_.version[2] == kProductUltralight) {
        kind_ = Kind::Ev1;
    }
}

const NfcTag& NfcType2Emulator::tag() const {
    return tag_;
}

uint32_t NfcType2Emulator::reads() const {
    return reads_;
}

uint32_t NfcType2Emulator::writes() const {
    return writes_;
}

void NfcType2Emulator::begin_field() {
    counted_this_field_ = false;
}

void NfcType2Emulator::begin_selection() {
    authenticated_ = false;
    ulc_pending_ = false;
}

NfcType2Emulator::Reply NfcType2Emulator::nak(uint8_t code) const {
    Reply reply;
    reply.kind = ReplyKind::Nak;
    reply.nibble = code;
    return reply;
}

uint8_t NfcType2Emulator::access() const {
    if ((kind_ != Kind::Ntag21x && kind_ != Kind::Ev1) || tag_.pages_total < 4) {
        return 0;
    }
    return tag_.data[static_cast<std::size_t>(tag_.pages_total - 3) * kNfcPageSize];
}

bool NfcType2Emulator::page_hidden(std::size_t page) const {
    if (kind_ == Kind::UltralightC) {
        return page >= kUlcKeyPage;
    }
    /* Password and PACK (the last two pages) always read as zeros on a real tag. */
    return tag_.has_version && tag_.pages_total >= 2 && page + 2 >= tag_.pages_total;
}

bool NfcType2Emulator::ulc_allowed(std::size_t page, bool write) const {
    if (page >= kUlcKeyPage) {
        return false;
    }
    const uint8_t auth0 = tag_.data[kUlcAuth0Page * kNfcPageSize];
    /* AUTH1 bit 0: 1 protects writes only, 0 reads and writes. */
    const bool writes_only = (tag_.data[kUlcAuth1Page * kNfcPageSize] & 0x01) != 0;
    if (auth0 < 0x03 || auth0 >= kUlcPages || authenticated_ || page < auth0) {
        return true;
    }
    return writes_only && !write;
}

NfcType2Emulator::Reply NfcType2Emulator::read_pages(std::size_t first, std::size_t count) {
    const std::size_t pages = tag_.pages_total;
    Reply reply;
    reply.kind = ReplyKind::Data;
    reply.data.assign(count * kNfcPageSize, 0);
    for (std::size_t index = 0; index < count; ++index) {
        /* READ wraps to page 0 after the last page, as the real tags do. */
        const std::size_t page = (first + index) % pages;
        if (page_hidden(page) || (kind_ == Kind::UltralightC && !ulc_allowed(page, false))) {
            continue;
        }
        std::copy_n(tag_.data.begin() + static_cast<std::ptrdiff_t>(page * kNfcPageSize),
                    kNfcPageSize,
                    reply.data.begin() + static_cast<std::ptrdiff_t>(index * kNfcPageSize));
    }
    /* The NFC counter of an NTAG21x counts the first read in each field, when enabled. */
    if (kind_ == Kind::Ntag21x && (access() & kAccessNfcCounterEnabled) != 0 && !counted_this_field_) {
        if (tag_.counters[2] < kCounterMax) {
            ++tag_.counters[2];
        }
        counted_this_field_ = true;
    }
    ++reads_;
    return reply;
}

NfcType2Emulator::Reply NfcType2Emulator::ulc_auth_start() {
    /* Step one: the tag's random number, encrypted with the key from IV 0. */
    std::random_device random;
    for (auto& byte : ulc_rnd_b_) {
        byte = static_cast<uint8_t>(random());
    }
    const uint8_t iv[8] = {};
    des3_cbc_encrypt(tag_.data.data() + kUlcKeyPage * kNfcPageSize, iv, ulc_rnd_b_.data(), 8, ulc_enc_b_.data());
    ulc_pending_ = true;

    Reply reply;
    reply.kind = ReplyKind::Data;
    reply.data.push_back(kUlcAuthContinue);
    reply.data.insert(reply.data.end(), ulc_enc_b_.begin(), ulc_enc_b_.end());
    return reply;
}

NfcType2Emulator::Reply NfcType2Emulator::ulc_auth_finish(const uint8_t* frame, std::size_t size) {
    /* Step two: the reader's RndA and the tag's RndB rotated by one byte, chained from step one. */
    if (size != 17 || tag_.pages_read < kUlcPages) {
        /* Without the key in the file the tag cannot prove itself. */
        return nak(kNakArgument);
    }
    const uint8_t* key = tag_.data.data() + kUlcKeyPage * kNfcPageSize;
    uint8_t plain[16];
    des3_cbc_decrypt(key, ulc_enc_b_.data(), frame + 1, 16, plain);

    /* Both random numbers travel rotated left by one byte. */
    for (std::size_t index = 0; index < 8; ++index) {
        if (plain[8 + index] != ulc_rnd_b_[(index + 1) % 8]) {
            return nak(kNakArgument);
        }
    }
    authenticated_ = true;

    uint8_t rotated_a[8];
    for (std::size_t index = 0; index < 8; ++index) {
        rotated_a[index] = plain[(index + 1) % 8];
    }
    uint8_t answer[8];
    des3_cbc_encrypt(key, frame + 1 + 8, rotated_a, 8, answer);

    Reply reply;
    reply.kind = ReplyKind::Data;
    reply.data.push_back(0x00);
    reply.data.insert(reply.data.end(), answer, answer + 8);
    return reply;
}

NfcType2Emulator::Reply NfcType2Emulator::handle(const uint8_t* frame, std::size_t size) {
    Reply reply;
    if (!frame || size == 0) {
        return reply;
    }
    const std::size_t pages = tag_.pages_total;

    /* The second half of an Ultralight C authentication must come next, or the attempt is over. */
    if (ulc_pending_) {
        ulc_pending_ = false;
        if (frame[0] == kUlcAuthContinue) {
            return ulc_auth_finish(frame, size);
        }
    }

    switch (frame[0]) {
        case kCmdHalt:
            /* HLTA is never answered. */
            reply.halt = true;
            return reply;

        case kCmdRead:
            if (size != 2 || frame[1] >= pages ||
                (kind_ == Kind::UltralightC && !ulc_allowed(frame[1], false))) {
                return nak(kNakArgument);
            }
            return read_pages(frame[1], 4);

        case kCmdFastRead: {
            if (!tag_.has_version || size != 3 || frame[1] > frame[2] || frame[2] >= pages) {
                return nak(kNakArgument);
            }
            const std::size_t count = static_cast<std::size_t>(frame[2]) - frame[1] + 1;
            if (count * kNfcPageSize > kMaxReplyBytes) {
                return nak(kNakArgument);
            }
            return read_pages(frame[1], count);
        }

        case kCmdGetVersion:
            if (!tag_.has_version || size != 1) {
                return nak(kNakArgument);
            }
            reply.kind = ReplyKind::Data;
            reply.data.assign(tag_.version.begin(), tag_.version.end());
            return reply;

        case kCmdReadSignature:
            if (!tag_.has_version || size != 2) {
                return nak(kNakArgument);
            }
            reply.kind = ReplyKind::Data;
            reply.data.assign(tag_.signature.begin(), tag_.signature.end());
            return reply;

        case kCmdReadCounter: {
            if (size != 2) {
                return nak(kNakArgument);
            }
            const uint8_t number = frame[1];
            const uint8_t settings = access();
            const bool ev1 = kind_ == Kind::Ev1 && number <= 2;
            /* An NTAG21x has only counter 2, readable when enabled, and maybe only after the password. */
            const bool ntag = kind_ == Kind::Ntag21x && number == 2 && (settings & kAccessNfcCounterEnabled) != 0 &&
                              ((settings & kAccessNfcCounterPasswordProtected) == 0 || authenticated_);
            if (!ev1 && !ntag) {
                return nak(kNakArgument);
            }
            const uint32_t value = tag_.counters[number];
            reply.kind = ReplyKind::Data;
            reply.data = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
                          static_cast<uint8_t>(value >> 16)};
            return reply;
        }

        case kCmdIncrementCounter: {
            /* Ultralight EV1 only: three bytes of increment, a fourth that is ignored. */
            if (kind_ != Kind::Ev1 || size != 6 || frame[1] > 2) {
                return nak(kNakArgument);
            }
            const uint32_t increment = frame[2] | (static_cast<uint32_t>(frame[3]) << 8) |
                                       (static_cast<uint32_t>(frame[4]) << 16);
            uint32_t& counter = tag_.counters[frame[1]];
            if (counter + increment > kCounterMax) {
                return nak(kNakOther);
            }
            counter += increment;
            reply.kind = ReplyKind::Ack;
            reply.nibble = kAck;
            return reply;
        }

        case kCmdCheckTearing: {
            if (size != 2) {
                return nak(kNakArgument);
            }
            const uint8_t number = frame[1];
            if (!(kind_ == Kind::Ev1 && number <= 2) && !(kind_ == Kind::Ntag21x && number == 2)) {
                return nak(kNakArgument);
            }
            reply.kind = ReplyKind::Data;
            reply.data.assign(1, tag_.tearing[number]);
            return reply;
        }

        case kCmdPasswordAuth: {
            /* Any password is accepted; the answer is the acknowledge stored in the last page. */
            if (!tag_.has_version || size != 5 || pages < 1) {
                return nak(kNakArgument);
            }
            authenticated_ = true;
            const uint8_t* pack = tag_.data.data() + (pages - 1) * kNfcPageSize;
            reply.kind = ReplyKind::Data;
            reply.data.assign(pack, pack + 2);
            return reply;
        }

        case kCmdUlcAuthenticate:
            if (kind_ != Kind::UltralightC || size != 2 || frame[1] != 0x00) {
                return nak(kNakArgument);
            }
            return ulc_auth_start();

        case kCmdWrite: {
            if (size != 6 || frame[1] < 2 || frame[1] >= pages ||
                (kind_ == Kind::UltralightC && !ulc_allowed(frame[1], true))) {
                return nak(kNakArgument);
            }
            uint8_t* page = tag_.data.data() + frame[1] * kNfcPageSize;
            if (frame[1] >= kFirstUserPage) {
                std::copy_n(frame + 2, kNfcPageSize, page);
            }
            else {
                /* Lock bytes and the one-time-programmable page: bits can only be set. */
                for (std::size_t index = frame[1] == 2 ? 2 : 0; index < kNfcPageSize; ++index) {
                    page[index] = static_cast<uint8_t>(page[index] | frame[2 + index]);
                }
            }
            ++writes_;
            reply.kind = ReplyKind::Ack;
            reply.nibble = kAck;
            return reply;
        }

        default:
            break;
    }
    return nak(kNakOther);
}

/* ---- ISO 14443-4A ----------------------------------------------------------- */

namespace {

constexpr uint8_t kCmdRats = 0xE0;
constexpr uint8_t kAtsT0HasTc1 = 0x40;
constexpr uint8_t kAtsT0HasTb1 = 0x20;
constexpr uint8_t kAtsT0HasTa1 = 0x10;
constexpr uint8_t kAtsTc1Cid = 0x02;

} // namespace

IsoDepEmulator::IsoDepEmulator(std::vector<uint8_t> ats) : ats_(std::move(ats)) {}

void IsoDepEmulator::begin_selection() {
    active_ = false;
    can_pps_ = false;
    cid_ = -1;
}

uint32_t IsoDepEmulator::rats() const {
    return rats_;
}

uint32_t IsoDepEmulator::blocks() const {
    return blocks_;
}

IsoDepEmulator::Reply IsoDepEmulator::handle(const uint8_t* frame, std::size_t size) {
    Reply reply;
    if (!frame || size == 0) {
        return reply;
    }

    if (!active_) {
        if (frame[0] == kCmdHalt && size == 2) {
            reply.halt = true;
        }
        else if (frame[0] == kCmdRats && size == 2 && !ats_.empty()) {
            active_ = true;
            can_pps_ = true;
            ++rats_;
            /* A CID is only used when the ATS says the card supports one (TC1). */
            bool cid_supported = false;
            if (ats_.size() > 1 && (ats_[1] & kAtsT0HasTc1) != 0) {
                std::size_t tc1 = 2;
                tc1 += (ats_[1] & kAtsT0HasTa1) != 0 ? 1 : 0;
                tc1 += (ats_[1] & kAtsT0HasTb1) != 0 ? 1 : 0;
                cid_supported = tc1 < ats_.size() && (ats_[tc1] & kAtsTc1Cid) != 0;
            }
            cid_ = cid_supported ? (frame[1] & 0x0F) : -1;
            reply.data = ats_;
        }
        return reply;
    }

    const uint8_t pcb = frame[0];
    /* PPS, only right after the ATS: acknowledged; the bit rate stays at 106 kbit/s. */
    if ((pcb & 0xF0) == 0xD0) {
        if (can_pps_ && (cid_ < 0 || (pcb & 0x0F) == cid_)) {
            reply.data.assign(1, pcb);
        }
        can_pps_ = false;
        return reply;
    }
    can_pps_ = false;

    /* A block for another CID is not for this card. */
    const bool has_cid = (pcb & 0x08) != 0;
    if (has_cid && (size < 2 || cid_ < 0 || (frame[1] & 0x0F) != cid_)) {
        return reply;
    }

    switch (pcb & 0xC0) {
        case 0x00:
            /* I-block: application data, which a dump does not contain. */
            ++blocks_;
            break;
        case 0xC0:
            if ((pcb & 0x30) == 0x00) {
                /* S(DESELECT): echoed, then the card sleeps. */
                reply.data.assign(frame, frame + size);
                reply.halt = true;
                active_ = false;
            }
            break;
        case 0x80: {
            /* R-block: nothing is chained, so the answer is a NAK with the reader's block number. */
            reply.data.push_back(static_cast<uint8_t>(0xB2 | (pcb & 0x09)));
            if (has_cid) {
                reply.data.push_back(frame[1]);
            }
            break;
        }
        default:
            break;
    }
    return reply;
}

} // namespace model
