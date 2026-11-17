/*
 * SPDX-License-Identifier: MIT
 */

#include "nfc_worker.h"

#include "nfc/des.h"
#include "nfc/felica.h"
#include "nfc/iso15693.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <random>
#include <cstdio>
#include <cstring>
#include <vector>

#if !USE_DESKTOP && defined(__linux__)
#define ZEROSUB_HAVE_NFC_DEVICE 1
#else
#define ZEROSUB_HAVE_NFC_DEVICE 0
#endif

#if ZEROSUB_HAVE_NFC_DEVICE
#include <cerrno>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace platform {
namespace {

/* How often an empty field is polled for a tag. */
constexpr unsigned int kTagPollMs = 120;
/* Answer times: a few milliseconds for a read, longer while a tag programs its memory. */
constexpr unsigned int kCommandTimeoutMs = 10;
constexpr unsigned int kWriteTimeoutMs = 25;
/* Polls that fail in a row before the field is switched off and on again. */
constexpr int kErrorsBeforeFieldReset = 3;

constexpr uint8_t kCmdSelectLevel1 = 0x93;
constexpr uint8_t kCmdRead = 0x30;
constexpr uint8_t kCmdWrite = 0xA2;
constexpr uint8_t kCmdGetVersion = 0x60;
constexpr uint8_t kCmdReadSignature = 0x3C;
constexpr uint8_t kCmdReadCounter = 0x39;
constexpr uint8_t kCmdCheckTearing = 0x3E;
constexpr uint8_t kCmdUlcAuthenticate = 0x1A;
constexpr uint8_t kUlcAuthContinue = 0xAF;
constexpr uint8_t kCmdRats = 0xE0;
constexpr uint8_t kCmdDeselect = 0xC2;
constexpr uint8_t kCmdHalt = 0x50;

/* GET_VERSION product types, and the NTAG21x storage sizes that have the NFC counter. */
constexpr uint8_t kProductUltralight = 0x03;
constexpr uint8_t kProductNtag = 0x04;

/* Ultralight C: 48 pages, of which 44-47 hold the 3DES key and are never readable. */
constexpr uint16_t kUlcPages = 48;
constexpr std::size_t kUlcKeyPage = 44;
/*
 * Keys tried on an Ultralight C, in the order the 3DES uses them (pages
 * 44-47 as Flipper Zero stores them): the NXP factory default
 * "BREAKMEIFYOUCAN!", the byte-reversed form some tools write, and all zeros.
 * A wrong key costs nothing: Ultralight C has no attempt limit.
 */
constexpr std::array<std::array<uint8_t, 16>, 3> kUlcKeys = {{
    {0x42, 0x52, 0x45, 0x41, 0x4B, 0x4D, 0x45, 0x49, 0x46, 0x59, 0x4F, 0x55, 0x43, 0x41, 0x4E, 0x21},
    {0x49, 0x45, 0x4B, 0x41, 0x45, 0x52, 0x42, 0x21, 0x4E, 0x41, 0x43, 0x55, 0x4F, 0x59, 0x46, 0x29},
    {},
}};
constexpr uint8_t kCascadeTag = 0x88;
constexpr uint8_t kAck = 0x0A;

constexpr uint16_t kFirstUserPage = 4;
/* Page addresses are one byte. */
constexpr uint16_t kMaxPages = 256;

using RxStatus = St25r3916::RxStatus;
using RxFrame = St25r3916::RxFrame;

enum class Activation {
    NoTag,
    Ok,
    /* More than one tag answered. */
    Collision,
    Error,
};

/* WUPA, anticollision and SELECT through every cascade level. Fills UID, ATQA and SAK. */
Activation activate(St25r3916& chip, model::NfcTag& tag) {
    RxFrame rx;
    const RxStatus wake = chip.wake_up(rx);
    if (wake == RxStatus::Timeout) {
        return Activation::NoTag;
    }
    if (wake == RxStatus::Collision) {
        return Activation::Collision;
    }
    if (wake != RxStatus::Ok || rx.data.size() != 2 || rx.last_bits != 0) {
        return Activation::Error;
    }
    tag.atqa = {rx.data[0], rx.data[1]};
    tag.uid.clear();

    for (uint8_t level = 0; level < 3; ++level) {
        const uint8_t select = static_cast<uint8_t>(kCmdSelectLevel1 + 2 * level);
        const uint8_t anticollision[2] = {select, 0x20};
        const RxStatus status = chip.exchange(anticollision, sizeof(anticollision), false, true, 10, rx);
        if (status == RxStatus::Collision) {
            return Activation::Collision;
        }
        if (status != RxStatus::Ok || rx.data.size() != 5 || rx.last_bits != 0 ||
            model::nfc_bcc(rx.data.data()) != rx.data[4]) {
            return Activation::Error;
        }

        const bool cascade = rx.data[0] == kCascadeTag;
        tag.uid.insert(tag.uid.end(), rx.data.begin() + (cascade ? 1 : 0), rx.data.begin() + 4);

        uint8_t frame[7] = {select, 0x70};
        std::copy_n(rx.data.begin(), 5, frame + 2);
        if (chip.exchange(frame, sizeof(frame), true, false, 10, rx) != RxStatus::Ok ||
            rx.data.size() != 3 || model::nfc_crc_a(rx.data.data(), rx.data.size()) != 0) {
            return Activation::Error;
        }
        const uint8_t sak = rx.data[0];
        const bool more = (sak & 0x04) != 0;
        if (more != cascade) {
            return Activation::Error;
        }
        if (!more) {
            tag.sak = sak;
            return Activation::Ok;
        }
    }
    return Activation::Error;
}

/* Selects the tag again after a command it refused; false if it is gone or another tag answers. */
bool reactivate(St25r3916& chip, const model::NfcTag& tag) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        model::NfcTag again;
        if (activate(chip, again) == Activation::Ok) {
            return again.uid == tag.uid;
        }
    }
    return false;
}

enum class CommandResult {
    Ok,
    /* The tag refused the command. It then needs a new selection. */
    Nak,
    /* No answer, or a damaged one. The tag also needs a new selection. */
    Failed,
};

/*
 * Sends one command to the selected tag. `answer_size` is the expected
 * payload without CRC; 0 means the answer is a four-bit acknowledge.
 */
CommandResult command(St25r3916& chip,
                      const uint8_t* tx,
                      std::size_t size,
                      std::size_t answer_size,
                      unsigned int timeout_ms,
                      std::vector<uint8_t>& answer) {
    answer.clear();
    RxFrame rx;
    if (chip.exchange(tx, size, true, false, timeout_ms, rx) != RxStatus::Ok) {
        return CommandResult::Failed;
    }
    if (rx.data.size() == 1 && rx.last_bits == 4) {
        const bool ack = (rx.data[0] & 0x0F) == kAck;
        return ack && answer_size == 0 ? CommandResult::Ok : CommandResult::Nak;
    }
    if (answer_size == 0 || rx.last_bits != 0 || rx.data.size() != answer_size + 2 ||
        model::nfc_crc_a(rx.data.data(), rx.data.size()) != 0) {
        return CommandResult::Failed;
    }
    answer.assign(rx.data.begin(), rx.data.end() - 2);
    return CommandResult::Ok;
}

CommandResult read_pages(St25r3916& chip, uint16_t page, std::vector<uint8_t>& answer) {
    const uint8_t tx[2] = {kCmdRead, static_cast<uint8_t>(page)};
    return command(chip, tx, sizeof(tx), 4 * model::kNfcPageSize, kCommandTimeoutMs, answer);
}

void halt(St25r3916& chip) {
    const uint8_t tx[2] = {kCmdHalt, 0x00};
    chip.transmit(tx, sizeof(tx), true);
}

/* GET_VERSION. Leaves the tag selected either way; false if the tag was lost. */
bool read_version(St25r3916& chip, model::NfcTag& tag) {
    std::vector<uint8_t> answer;
    const uint8_t tx[1] = {kCmdGetVersion};
    tag.has_version = command(chip, tx, sizeof(tx), tag.version.size(), kCommandTimeoutMs, answer) ==
                      CommandResult::Ok;
    if (tag.has_version) {
        std::copy(answer.begin(), answer.end(), tag.version.begin());
        return true;
    }
    /* The original Ultralight and the NTAG203 do not know the command and drop the selection. */
    return reactivate(chip, tag);
}

/*
 * Ultralight C, first step of AUTHENTICATE: the tag's RndB, encrypted. Only
 * an Ultralight C answers; any other tag refuses and drops the selection.
 */
bool ulc_challenge(St25r3916& chip, std::array<uint8_t, 8>& enc_b) {
    std::vector<uint8_t> answer;
    const uint8_t tx[2] = {kCmdUlcAuthenticate, 0x00};
    if (command(chip, tx, sizeof(tx), 9, kWriteTimeoutMs, answer) != CommandResult::Ok || answer[0] != kUlcAuthContinue) {
        return false;
    }
    std::copy(answer.begin() + 1, answer.end(), enc_b.begin());
    return true;
}

/*
 * Second step: proves `key` to the tag and checks the tag's proof. The tag
 * stays selected and unlocked on success, and drops the selection otherwise.
 */
bool ulc_authenticate(St25r3916& chip, const std::array<uint8_t, 16>& key, const std::array<uint8_t, 8>& enc_b) {
    const uint8_t zero_iv[8] = {};
    uint8_t rnd_b[8];
    model::des3_cbc_decrypt(key.data(), zero_iv, enc_b.data(), 8, rnd_b);

    std::random_device random;
    uint8_t plain[16];
    for (std::size_t index = 0; index < 8; ++index) {
        plain[index] = static_cast<uint8_t>(random());
        plain[8 + index] = rnd_b[(index + 1) % 8];
    }
    uint8_t tx[17];
    tx[0] = kUlcAuthContinue;
    model::des3_cbc_encrypt(key.data(), enc_b.data(), plain, 16, tx + 1);

    std::vector<uint8_t> answer;
    if (command(chip, tx, sizeof(tx), 9, kWriteTimeoutMs, answer) != CommandResult::Ok || answer[0] != 0x00) {
        return false;
    }
    uint8_t rnd_a[8];
    model::des3_cbc_decrypt(key.data(), tx + 1 + 8, answer.data() + 1, 8, rnd_a);
    for (std::size_t index = 0; index < 8; ++index) {
        if (rnd_a[index] != plain[(index + 1) % 8]) {
            return false;
        }
    }
    return true;
}

/*
 * Identifies an Ultralight C among the tags without GET_VERSION and unlocks
 * it with the first key in kUlcKeys that works. Leaves the tag selected
 * (and unlocked when `key_found`); false if the tag was lost.
 */
bool probe_ultralight_c(St25r3916& chip, const model::NfcTag& tag, bool& is_ulc, bool& key_found,
                        std::array<uint8_t, 16>& key) {
    is_ulc = false;
    key_found = false;
    std::array<uint8_t, 8> enc_b{};
    if (!ulc_challenge(chip, enc_b)) {
        return reactivate(chip, tag);
    }
    is_ulc = true;
    for (std::size_t index = 0; index < kUlcKeys.size(); ++index) {
        if (index != 0 && (!reactivate(chip, tag) || !ulc_challenge(chip, enc_b))) {
            return false;
        }
        if (ulc_authenticate(chip, kUlcKeys[index], enc_b)) {
            key = kUlcKeys[index];
            key_found = true;
            return true;
        }
    }
    return reactivate(chip, tag);
}

/*
 * The one-way counters and their tearing flags: counter 2 on an NTAG213 /
 * 215 / 216 (when the tag enables it), all three on an Ultralight EV1.
 * Refused commands only mean the counter is not readable. False if the tag
 * was lost.
 */
bool read_counters(St25r3916& chip, model::NfcTag& tag) {
    if (!tag.has_version) {
        return true;
    }
    const bool ntag21x = tag.version[2] == kProductNtag && tag.version[6] >= 0x0F;
    const bool ev1 = tag.version[2] == kProductUltralight;
    if (!ntag21x && !ev1) {
        return true;
    }
    std::vector<uint8_t> answer;
    for (uint8_t number = ntag21x ? 2 : 0; number < 3; ++number) {
        const uint8_t counter[2] = {kCmdReadCounter, number};
        if (command(chip, counter, sizeof(counter), 3, kCommandTimeoutMs, answer) == CommandResult::Ok) {
            tag.counters[number] = answer[0] | (static_cast<uint32_t>(answer[1]) << 8) |
                                   (static_cast<uint32_t>(answer[2]) << 16);
        }
        else if (!reactivate(chip, tag)) {
            return false;
        }
        const uint8_t tearing[2] = {kCmdCheckTearing, number};
        if (command(chip, tearing, sizeof(tearing), 1, kCommandTimeoutMs, answer) == CommandResult::Ok) {
            tag.tearing[number] = answer[0];
        }
        else if (!reactivate(chip, tag)) {
            return false;
        }
    }
    return true;
}

/*
 * ISO 14443-4 cards (SAK bit 0x20): RATS for the ATS, which is what a reader
 * checks besides UID and SAK, then DESELECT. A card that does not answer is
 * kept without an ATS.
 */
void read_ats(St25r3916& chip, model::NfcTag& tag) {
    /* FSDI 8 (256-byte frames), CID 0. */
    const uint8_t rats[2] = {kCmdRats, 0x80};
    RxFrame rx;
    if (chip.exchange(rats, sizeof(rats), true, false, kCommandTimeoutMs, rx) != RxStatus::Ok ||
        rx.last_bits != 0 || rx.data.size() < 3 || model::nfc_crc_a(rx.data.data(), rx.data.size()) != 0) {
        return;
    }
    std::vector<uint8_t> ats(rx.data.begin(), rx.data.end() - 2);
    if (ats[0] != ats.size()) {
        return;
    }
    tag.ats = std::move(ats);
    const uint8_t deselect[1] = {kCmdDeselect};
    chip.exchange(deselect, sizeof(deselect), true, false, kCommandTimeoutMs, rx);
}

/* ---- NFC-F (FeliCa) ---------------------------------------------------------- */

enum class FelicaRead {
    NoTag,
    Ok,
    Collision,
    Lost,
};

/* Limits on what a Standard card's public services may take, so a large card cannot keep the reader busy. */
constexpr std::size_t kFelicaMaxSearch = 512;
constexpr std::size_t kFelicaMaxBlocksPerService = 64;
constexpr std::size_t kFelicaMaxPublicBlocks = 512;

/*
 * One FeliCa exchange: the length byte is added in front, the chip adds the
 * CRC. The answer comes back without length byte and CRC, both checked.
 */
RxStatus felica_exchange(St25r3916& chip, const std::vector<uint8_t>& payload, std::vector<uint8_t>& answer) {
    answer.clear();
    std::vector<uint8_t> frame;
    frame.push_back(static_cast<uint8_t>(payload.size() + 1));
    frame.insert(frame.end(), payload.begin(), payload.end());
    RxFrame rx;
    const RxStatus status = chip.exchange(frame.data(), frame.size(), true, false, kCommandTimeoutMs, rx);
    if (status != RxStatus::Ok) {
        return status;
    }
    const std::size_t size = rx.data.size();
    if (rx.last_bits != 0 || size < 4 || rx.data[0] != size - 2 ||
        model::felica_crc(rx.data.data(), size - 2) != static_cast<uint16_t>((rx.data[size - 2] << 8) | rx.data[size - 1])) {
        return RxStatus::Error;
    }
    answer.assign(rx.data.begin() + 1, rx.data.end() - 2);
    return RxStatus::Ok;
}

std::vector<uint8_t> felica_command(uint8_t command, const std::array<uint8_t, 8>& idm, std::initializer_list<uint8_t> rest) {
    std::vector<uint8_t> payload = {command};
    payload.insert(payload.end(), idm.begin(), idm.end());
    payload.insert(payload.end(), rest.begin(), rest.end());
    return payload;
}

/*
 * READ WITHOUT ENCRYPTION of one block of one service. Returns the status
 * flags (SF1 0 means success) and fills `data`; false when there is no
 * proper answer at all.
 */
bool felica_read_block(St25r3916& chip, const std::array<uint8_t, 8>& idm, uint16_t service, uint8_t block,
                       uint8_t& sf1, uint8_t& sf2, std::array<uint8_t, model::kFelicaBlockSize>& data) {
    std::vector<uint8_t> answer;
    const auto request = felica_command(0x06, idm, {0x01, static_cast<uint8_t>(service), static_cast<uint8_t>(service >> 8),
                                                    0x01, 0x80, block});
    if (felica_exchange(chip, request, answer) != RxStatus::Ok || answer.size() < 11 || answer[0] != 0x07) {
        return false;
    }
    sf1 = answer[9];
    sf2 = answer[10];
    data.fill(0);
    if (sf1 == 0x00 && answer.size() >= 12 + model::kFelicaBlockSize) {
        std::copy_n(answer.begin() + 12, model::kFelicaBlockSize, data.begin());
    }
    return true;
}

/* The system's areas and services, through SEARCH SERVICE CODE. */
void felica_list_services(St25r3916& chip, const std::array<uint8_t, 8>& idm, model::FelicaSystem& system) {
    std::vector<uint8_t> answer;
    for (std::size_t index = 0; index < kFelicaMaxSearch; ++index) {
        const auto request = felica_command(0x0A, idm, {static_cast<uint8_t>(index), static_cast<uint8_t>(index >> 8)});
        if (felica_exchange(chip, request, answer) != RxStatus::Ok || answer.size() < 11 || answer[0] != 0x0B) {
            break;
        }
        const uint16_t code = static_cast<uint16_t>(answer[9] | (answer[10] << 8));
        if (code == 0xFFFF) {
            break;
        }
        if (answer.size() >= 13) {
            /* An area: its first and last code. */
            model::FelicaArea area;
            area.code = code;
            area.end = static_cast<uint16_t>(answer[11] | (answer[12] << 8));
            area.first = static_cast<uint16_t>(system.services.size());
            system.areas.push_back(area);
        }
        else {
            system.services.push_back(code);
        }
    }
    /* Each area's services are those whose number falls between the area's first and last. */
    for (auto& area : system.areas) {
        area.last = area.first;
        for (std::size_t index = area.first; index < system.services.size(); ++index) {
            const uint16_t number = system.services[index] >> 6;
            if (number >= (area.code >> 6) && number <= (area.end >> 6)) {
                area.last = static_cast<uint16_t>(index);
            }
        }
    }
}

/*
 * Polling, then: a Lite has its 28 blocks read; a Standard card lists its
 * systems, and in each its areas and services and the blocks of the public
 * ones.
 */
FelicaRead read_felica(St25r3916& chip, model::NfcTag& tag) {
    std::vector<uint8_t> answer;
    /* POLLING: any system (FFFF), asking for the system code, one time slot. */
    const RxStatus poll = felica_exchange(chip, {0x00, 0xFF, 0xFF, 0x01, 0x00}, answer);
    if (poll == RxStatus::Timeout) {
        return FelicaRead::NoTag;
    }
    if (poll == RxStatus::Collision) {
        return FelicaRead::Collision;
    }
    if (poll != RxStatus::Ok || answer.size() < 17 || answer[0] != 0x01) {
        return FelicaRead::Lost;
    }

    tag = model::NfcTag{};
    tag.technology = model::NfcTechnology::F;
    std::array<uint8_t, 8> idm{};
    std::copy_n(answer.begin() + 1, 8, idm.begin());
    tag.uid.assign(idm.begin(), idm.end());
    std::copy_n(answer.begin() + 9, 8, tag.felica_pmm.begin());
    const uint16_t polled_system = answer.size() >= 19 ? static_cast<uint16_t>((answer[17] << 8) | answer[18]) : 0xFFFF;

    uint8_t sf1 = 0;
    uint8_t sf2 = 0;
    std::array<uint8_t, model::kFelicaBlockSize> block{};
    if (model::felica_is_lite(tag.felica_pmm)) {
        tag.felica_lite = true;
        tag.block_count = model::kFelicaLiteBlocks;
        tag.block_size = model::kFelicaBlockSize;
        tag.data.assign(model::kFelicaLiteBlocks * model::kFelicaLiteRecord, 0);
        for (std::size_t index = 0; index < model::kFelicaLiteBlocks; ++index) {
            /* Service 000B: the Lite's read-only view of every block. */
            if (!felica_read_block(chip, idm, 0x000B, model::kFelicaLiteBlockNumbers[index], sf1, sf2, block)) {
                continue;
            }
            uint8_t* record = tag.data.data() + index * model::kFelicaLiteRecord;
            record[0] = sf1;
            record[1] = sf2;
            std::copy(block.begin(), block.end(), record + 2);
            tag.blocks_read = static_cast<uint16_t>(tag.blocks_read + (sf1 == 0 ? 1 : 0));
        }
        return FelicaRead::Ok;
    }

    /* REQUEST SYSTEM CODE: the systems on the card, each answering to its own IDm. */
    std::vector<uint16_t> codes;
    if (felica_exchange(chip, felica_command(0x0C, idm, {}), answer) == RxStatus::Ok && answer.size() >= 10 &&
        answer[0] == 0x0D) {
        for (std::size_t index = 0; index < answer[9] && 11 + 2 * index < answer.size(); ++index) {
            codes.push_back(static_cast<uint16_t>((answer[10 + 2 * index] << 8) | answer[11 + 2 * index]));
        }
    }
    if (codes.empty()) {
        codes.push_back(polled_system);
    }
    std::size_t public_blocks = 0;
    for (std::size_t system_index = 0; system_index < codes.size() && system_index < 16; ++system_index) {
        model::FelicaSystem system;
        system.code = codes[system_index];
        /* The system's number goes in the upper half of the IDm's first byte. */
        std::array<uint8_t, 8> system_idm = idm;
        system_idm[0] = static_cast<uint8_t>((idm[0] & 0x0F) | (system_index << 4));
        felica_list_services(chip, system_idm, system);
        for (uint16_t service : system.services) {
            if (!model::felica_service_public(service)) {
                continue;
            }
            for (std::size_t index = 0; index < kFelicaMaxBlocksPerService && public_blocks < kFelicaMaxPublicBlocks;
                 ++index) {
                if (!felica_read_block(chip, system_idm, service, static_cast<uint8_t>(index), sf1, sf2, block) ||
                    sf1 != 0x00) {
                    break;
                }
                system.blocks.push_back({service, static_cast<uint8_t>(index), block});
                ++public_blocks;
            }
        }
        tag.felica_systems.push_back(std::move(system));
    }
    return FelicaRead::Ok;
}

/* ---- NFC-B (ISO 14443-B and ST25TB) ---------------------------------------- */

enum class NfcbRead {
    NoTag,
    Ok,
    Collision,
    Lost,
};

/* One exchange in NFC-B mode: the chip adds the CRC_B, which comes back in the answer and is checked here. */
RxStatus nfcb_exchange(St25r3916& chip, const std::vector<uint8_t>& request, std::vector<uint8_t>& answer) {
    answer.clear();
    RxFrame rx;
    const RxStatus status = chip.exchange(request.data(), request.size(), true, false, kCommandTimeoutMs, rx);
    if (status != RxStatus::Ok) {
        return status;
    }
    /* CRC_B is the same CRC as ISO 15693's. */
    if (rx.last_bits != 0 || rx.data.size() < 3 ||
        model::iso15693_crc(rx.data.data(), rx.data.size() - 2) !=
            static_cast<uint16_t>(rx.data[rx.data.size() - 2] | (rx.data[rx.data.size() - 1] << 8))) {
        return RxStatus::Error;
    }
    answer.assign(rx.data.begin(), rx.data.end() - 2);
    return RxStatus::Ok;
}

/*
 * ST25TB / SRIX: INITIATE, SELECT, GET_UID, every block and the system
 * block 255, then COMPLETION. These tags do not answer REQB.
 */
NfcbRead read_st25tb(St25r3916& chip, model::NfcTag& tag) {
    std::vector<uint8_t> answer;
    const RxStatus initiate = nfcb_exchange(chip, {0x06, 0x00}, answer);
    if (initiate == RxStatus::Timeout) {
        return NfcbRead::NoTag;
    }
    if (initiate == RxStatus::Collision) {
        return NfcbRead::Collision;
    }
    if (initiate != RxStatus::Ok || answer.size() != 1) {
        return NfcbRead::Lost;
    }
    const uint8_t chip_id = answer[0];
    if (nfcb_exchange(chip, {0x0E, chip_id}, answer) != RxStatus::Ok || answer.size() != 1 || answer[0] != chip_id) {
        return NfcbRead::Lost;
    }
    if (nfcb_exchange(chip, {0x0B}, answer) != RxStatus::Ok || answer.size() != 8) {
        return NfcbRead::Lost;
    }

    tag = model::NfcTag{};
    tag.technology = model::NfcTechnology::B;
    tag.st25tb = true;
    /* Sent lowest byte first; printed D0 first. */
    tag.uid.assign(answer.rbegin(), answer.rend());
    const model::St25tbSpec* spec = model::nfc_st25tb_spec_for_uid(tag.uid);
    if (spec) {
        tag.st25tb_type = spec->type_name;
        tag.block_count = spec->blocks;
        tag.block_size = 4;
        tag.data.assign(static_cast<std::size_t>(spec->blocks) * 4, 0);
        for (uint16_t block = 0; block < spec->blocks; ++block) {
            if (nfcb_exchange(chip, {0x08, static_cast<uint8_t>(block)}, answer) == RxStatus::Ok &&
                answer.size() == 4) {
                std::copy(answer.begin(), answer.end(), tag.data.begin() + block * 4);
                ++tag.blocks_read;
            }
        }
        if (nfcb_exchange(chip, {0x08, 0xFF}, answer) == RxStatus::Ok && answer.size() == 4) {
            std::copy(answer.begin(), answer.end(), tag.st25tb_system.begin());
        }
    }
    /* COMPLETION: the tag stays silent until the field drops. */
    const uint8_t completion[1] = {0x0F};
    chip.transmit(completion, sizeof(completion), true);
    return NfcbRead::Ok;
}

/* WUPB for ISO 14443-3B cards, then HLTB; if nothing answers, the ST25TB commands. */
NfcbRead read_nfcb(St25r3916& chip, model::NfcTag& tag) {
    std::vector<uint8_t> answer;
    /* APf 05, AFI 00 (all families), PARAM 08: WUPB with one slot. */
    const RxStatus wake = nfcb_exchange(chip, {0x05, 0x00, 0x08}, answer);
    if (wake == RxStatus::Collision) {
        return NfcbRead::Collision;
    }
    if (wake == RxStatus::Ok && answer.size() >= 12 && answer[0] == 0x50) {
        tag = model::NfcTag{};
        tag.technology = model::NfcTechnology::B;
        tag.uid.assign(answer.begin() + 1, answer.begin() + 5);
        std::copy(answer.begin() + 5, answer.begin() + 9, tag.app_data.begin());
        std::copy(answer.begin() + 9, answer.begin() + 12, tag.protocol_info.begin());
        std::vector<uint8_t> halt = {0x50};
        halt.insert(halt.end(), tag.uid.begin(), tag.uid.end());
        nfcb_exchange(chip, halt, answer);
        return NfcbRead::Ok;
    }
    if (wake != RxStatus::Timeout) {
        return NfcbRead::Lost;
    }
    return read_st25tb(chip, tag);
}

/* ---- NFC-V (ISO 15693) ---------------------------------------------------- */

/* ISO 15693 tags answer within a few milliseconds; M5Stack allows 16-20. */
constexpr unsigned int kNfcvTimeoutMs = 20;
constexpr uint16_t kNfcvMaxBlocks = 256;

enum class NfcvRead {
    NoTag,
    Ok,
    /* Several tags answered the inventory. */
    Collision,
    /* A tag answered, then stopped answering. */
    Lost,
};

std::vector<uint8_t> nfcv_request(uint8_t flags, uint8_t command, const std::vector<uint8_t>& uid_air) {
    std::vector<uint8_t> request = {flags, command};
    request.insert(request.end(), uid_air.begin(), uid_air.end());
    return request;
}

/*
 * READ SINGLE BLOCK, with the option flag for the block's lock status while
 * the tag accepts it. `block_size` 0 accepts any size and sets it.
 */
bool nfcv_read_block(St25r3916& chip,
                     const std::vector<uint8_t>& uid_air,
                     uint16_t block,
                     bool& with_status,
                     uint8_t& block_size,
                     std::vector<uint8_t>& data,
                     uint8_t& status) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        auto request = nfcv_request(with_status ? model::kIso15693FlagsAddressedOption : model::kIso15693FlagsAddressed,
                                    model::kIso15693CmdReadSingleBlock,
                                    uid_air);
        request.push_back(static_cast<uint8_t>(block));
        std::vector<uint8_t> answer;
        const RxStatus result = chip.exchange_nfcv(request, kNfcvTimeoutMs, answer);
        if (result != RxStatus::Ok || answer.empty()) {
            continue;
        }
        if ((answer[0] & model::kIso15693ResponseError) != 0) {
            /* Some tags refuse the option flag; without it there is no lock status. */
            if (with_status && attempt == 0) {
                with_status = false;
                continue;
            }
            return false;
        }
        const std::size_t header = with_status ? 2 : 1;
        if (answer.size() <= header || (block_size != 0 && answer.size() != header + block_size)) {
            continue;
        }
        block_size = static_cast<uint8_t>(answer.size() - header);
        status = with_status ? answer[1] : 0;
        data.assign(answer.begin() + static_cast<std::ptrdiff_t>(header), answer.end());
        return true;
    }
    return false;
}

/*
 * Inventory with one slot, then GET SYSTEM INFORMATION and every block.
 * The field must be on in NFC-V mode.
 */
NfcvRead read_nfcv(St25r3916& chip, model::NfcTag& tag) {
    std::vector<uint8_t> answer;
    const RxStatus inventory = chip.exchange_nfcv(
        {model::kIso15693FlagsInventory, model::kIso15693CmdInventory, 0x00}, kNfcvTimeoutMs, answer);
    if (inventory == RxStatus::Timeout) {
        return NfcvRead::NoTag;
    }
    if (inventory == RxStatus::Collision) {
        return NfcvRead::Collision;
    }
    if (inventory != RxStatus::Ok || answer.size() != 10 || (answer[0] & model::kIso15693ResponseError) != 0) {
        return NfcvRead::Lost;
    }

    tag = model::NfcTag{};
    tag.technology = model::NfcTechnology::V;
    tag.has_dsfid = true;
    tag.dsfid = answer[1];
    const std::vector<uint8_t> uid_air(answer.begin() + 2, answer.end());
    tag.uid = model::iso15693_uid_from_air(uid_air.data());

    /* System information: what the tag tells about itself, the memory size above all. */
    if (chip.exchange_nfcv(nfcv_request(model::kIso15693FlagsAddressed, model::kIso15693CmdGetSystemInfo, uid_air),
                           kNfcvTimeoutMs,
                           answer) == RxStatus::Ok &&
        answer.size() >= 10 && (answer[0] & model::kIso15693ResponseError) == 0) {
        const uint8_t info = answer[1];
        std::size_t index = 10;
        if ((info & 0x01) != 0 && index < answer.size()) {
            tag.dsfid = answer[index++];
        }
        if ((info & 0x02) != 0 && index < answer.size()) {
            tag.has_afi = true;
            tag.afi = answer[index++];
        }
        if ((info & 0x04) != 0 && index + 1 < answer.size()) {
            tag.block_count = static_cast<uint16_t>(answer[index] + 1);
            tag.block_size = static_cast<uint8_t>((answer[index + 1] & 0x1F) + 1);
            index += 2;
        }
        if ((info & 0x08) != 0 && index < answer.size()) {
            tag.has_ic_ref = true;
            tag.ic_ref = answer[index++];
        }
    }

    /* The blocks; without a size from the tag, read until one is refused. */
    const bool size_known = tag.block_count != 0;
    const uint16_t limit = size_known ? tag.block_count : kNfcvMaxBlocks;
    bool with_status = true;
    std::vector<uint8_t> block_data;
    uint8_t status = 0;
    uint16_t block = 0;
    for (; block < limit; ++block) {
        if (!nfcv_read_block(chip, uid_air, block, with_status, tag.block_size, block_data, status)) {
            if (!size_known) {
                break;
            }
            /* A protected block stays zero; the rest may still be readable. */
            tag.data.resize(static_cast<std::size_t>(block + 1) * tag.block_size, 0);
            tag.block_security.push_back(0);
            continue;
        }
        tag.data.resize(static_cast<std::size_t>(block) * tag.block_size, 0);
        tag.data.insert(tag.data.end(), block_data.begin(), block_data.end());
        tag.block_security.push_back(status != 0 ? 0x01 : 0x00);
        ++tag.blocks_read;
    }
    if (!size_known) {
        tag.block_count = block;
    }
    if (tag.block_count != 0 && tag.blocks_read == 0 && tag.block_size == 0) {
        /* The size was reported but not a single block answered: the tag left. */
        return NfcvRead::Lost;
    }
    tag.data.resize(static_cast<std::size_t>(tag.block_count) * tag.block_size, 0);
    tag.block_security.resize(tag.block_count, 0);
    return NfcvRead::Ok;
}

/*
 * Reads the memory of a selected NTAG / Ultralight tag. Returns false when
 * the tag was lost on the way; a tag that turns out not to be an
 * NTAG / Ultralight is a success with `type2` left false.
 */
bool read_type2(St25r3916& chip, model::NfcTag& tag) {
    tag.type2 = false;
    if (!read_version(chip, tag)) {
        return false;
    }

    std::vector<uint8_t> answer;
    if (tag.has_version) {
        const uint8_t tx[2] = {kCmdReadSignature, 0x00};
        tag.has_signature = command(chip, tx, sizeof(tx), tag.signature.size(), kCommandTimeoutMs, answer) ==
                            CommandResult::Ok;
        if (tag.has_signature) {
            std::copy(answer.begin(), answer.end(), tag.signature.begin());
        }
        else if (!reactivate(chip, tag)) {
            return false;
        }
        if (!read_counters(chip, tag)) {
            return false;
        }
    }

    /* Without GET_VERSION it may be an Ultralight C; unlocked, its protected pages can be read too. */
    bool ulc = false;
    bool ulc_key_found = false;
    std::array<uint8_t, 16> ulc_key{};
    if (!tag.has_version && !probe_ultralight_c(chip, tag, ulc, ulc_key_found, ulc_key)) {
        return false;
    }

    const model::NfcType2Spec* spec = tag.has_version ? model::nfc_type2_spec_for_version(tag.version) : nullptr;
    const uint16_t known_total = spec ? spec->pages_total : (ulc ? kUlcPages : 0);

    /* READ returns four pages at a time and wraps to page 0 past the last one. */
    std::vector<uint8_t> data;
    uint16_t page = 0;
    while (page < (known_total != 0 ? known_total : kMaxPages)) {
        if (read_pages(chip, page, answer) != CommandResult::Ok) {
            break;
        }
        data.insert(data.end(), answer.begin(), answer.end());
        page = static_cast<uint16_t>(page + 4);
    }
    if (page == 0) {
        /* SAK 00 but no Type 2 READ: some other kind of tag. Keep it as UID only. */
        tag.has_version = false;
        tag.has_signature = false;
        return reactivate(chip, tag);
    }

    uint16_t total = known_total;
    uint16_t pages_read = known_total != 0 ? std::min(page, known_total) : page;
    if (known_total == 0) {
        /*
         * Size unknown: the first refused READ is past the end, so the last
         * block may have wrapped. The highest page that can still be read by
         * itself is the last one.
         */
        total = 0;
        for (uint16_t probe = static_cast<uint16_t>(page - 1); probe + 4 > page; --probe) {
            if (!reactivate(chip, tag)) {
                return false;
            }
            if (read_pages(chip, probe, answer) == CommandResult::Ok) {
                total = static_cast<uint16_t>(probe + 1);
                break;
            }
            if (probe == 0) {
                break;
            }
        }
        if (total == 0) {
            return false;
        }
        pages_read = total;
    }
    else if (pages_read < known_total && !reactivate(chip, tag)) {
        /* A password-protected tag refuses the pages behind its protection. */
        return false;
    }

    data.resize(static_cast<std::size_t>(total) * model::kNfcPageSize, 0);
    if (ulc && ulc_key_found) {
        /* The key itself is never readable, but it is known now: stored where Flipper Zero keeps it. */
        std::copy(ulc_key.begin(), ulc_key.end(), data.begin() + kUlcKeyPage * model::kNfcPageSize);
        pages_read = kUlcPages;
    }
    tag.type2 = true;
    tag.type_name = spec ? spec->name : (ulc ? "Mifare Ultralight C" : model::nfc_type2_name_for_pages(total));
    tag.pages_total = total;
    tag.pages_read = pages_read;
    tag.data = std::move(data);
    return true;
}

#if ZEROSUB_HAVE_NFC_DEVICE

constexpr const char* kDefaultSpiNode = "/dev/spidev0.2";
constexpr const char* kOverlayDirectory = "/boot/firmware/overlays";
constexpr const char* kOverlayName = "spi0-spidev2-gpio22-overlay";
constexpr const char* kOverlayFile = "/boot/firmware/overlays/spi0-spidev2-gpio22-overlay.dtbo";
constexpr const char* kDtoverlay = "/usr/bin/dtoverlay";
constexpr const char* kSudo = "/usr/bin/sudo";

/*
 * The reader sits on a third chip select of SPI0 that the stock device tree
 * does not have. The BSP ships an overlay adding it (CS2 on GPIO22); this
 * loads it when the spidev node is missing. It stays loaded until reboot.
 */
bool ensure_spi_node(const std::string& path, const std::atomic<bool>& cancel, std::string& error) {
    if (::access(path.c_str(), F_OK) == 0) {
        return true;
    }
    const std::string hint =
        std::string("; add dtoverlay=") + kOverlayName + " to /boot/firmware/config.txt and reboot";
    if (path != kDefaultSpiNode) {
        error = path + " does not exist";
        return false;
    }
    if (::access(kOverlayFile, F_OK) != 0) {
        error = path + " is missing and the system has no " + kOverlayName + " overlay";
        return false;
    }
    /* Loading an overlay needs root: run dtoverlay directly as root, otherwise
       through `sudo -n`, which fails instead of asking for a password. */
    std::vector<char*> argv;
    if (::geteuid() != 0) {
        argv.push_back(const_cast<char*>(kSudo));
        argv.push_back(const_cast<char*>("-n"));
        argv.push_back(const_cast<char*>("--"));
    }
    argv.push_back(const_cast<char*>(kDtoverlay));
    argv.push_back(const_cast<char*>("-d"));
    argv.push_back(const_cast<char*>(kOverlayDirectory));
    argv.push_back(const_cast<char*>(kOverlayName));
    argv.push_back(nullptr);
    pid_t child = -1;
    if (::posix_spawn(&child, argv[0], nullptr, nullptr, argv.data(), environ) != 0) {
        error = std::string("could not run dtoverlay") + hint;
        return false;
    }

    /* dtoverlay normally returns within a second. */
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
    int status = 0;
    for (;;) {
        const pid_t result = ::waitpid(child, &status, WNOHANG);
        if (result == child) {
            break;
        }
        const bool gone = result < 0 && errno != EINTR;
        if (gone || cancel.load() || std::chrono::steady_clock::now() >= deadline) {
            if (!gone) {
                ::kill(child, SIGKILL);
                ::waitpid(child, &status, 0);
            }
            error = std::string("loading the SPI overlay did not finish") + hint;
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    /* The node appears a moment after the overlay is applied. */
    for (int attempt = 0; attempt < 100 && !cancel.load(); ++attempt) {
        if (::access(path.c_str(), F_OK) == 0) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    error = path + " did not appear after loading the SPI overlay" + hint;
    return false;
}

/* Emulation answers within the reader's timeout, so its thread should not wait behind others. */
void raise_thread_priority() {
    sched_param parameters;
    std::memset(&parameters, 0, sizeof(parameters));
    parameters.sched_priority = 50;
    /* Without the right to do this the thread simply keeps its normal priority. */
    (void)::pthread_setschedparam(::pthread_self(), SCHED_FIFO, &parameters);
}

#else // !ZEROSUB_HAVE_NFC_DEVICE

bool ensure_spi_node(const std::string&, const std::atomic<bool>&, std::string&) {
    /* Opening the chip reports that there is no SPI on this build. */
    return true;
}

void raise_thread_priority() {}

#endif

} // namespace

std::unique_ptr<NfcWorker> NfcWorker::start(const St25r3916Config& config,
                                            Job job,
                                            const model::NfcTag& source,
                                            unsigned int settle_ms) {
    return std::unique_ptr<NfcWorker>(new NfcWorker(config, job, source, settle_ms));
}

NfcWorker::NfcWorker(const St25r3916Config& config,
                     Job job,
                     const model::NfcTag& source,
                     unsigned int settle_ms)
    : config_(config), job_(job), source_(source), settle_ms_(settle_ms) {
    status_.text = "Starting the NFC reader...";
    thread_ = std::thread([this] {
        run();
        running_.store(false);
        revision_.fetch_add(1);
    });
}

NfcWorker::~NfcWorker() {
    cancel_.store(true);
    if (thread_.joinable()) {
        thread_.join();
    }
}

NfcWorker::Job NfcWorker::job() const {
    return job_;
}

uint32_t NfcWorker::revision() const {
    return revision_.load();
}

NfcWorker::Status NfcWorker::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

bool NfcWorker::running() const {
    return running_.load();
}

bool NfcWorker::cancelled() const {
    return cancel_.load();
}

bool NfcWorker::sleep_for(unsigned int duration_ms) const {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(duration_ms);
    while (!cancelled()) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return true;
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        std::this_thread::sleep_for(std::min(remaining, std::chrono::milliseconds(20)));
    }
    return false;
}

void NfcWorker::publish(Phase phase, const std::string& text) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (status_.phase == phase && status_.text == text) {
            return;
        }
        status_.phase = phase;
        status_.text = text;
    }
    revision_.fetch_add(1);
}

void NfcWorker::publish_tag(const model::NfcTag& tag, const std::string& text) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.phase = Phase::Done;
        status_.text = text;
        status_.tag = tag;
    }
    revision_.fetch_add(1);
}

void NfcWorker::run() {
    if (!sleep_for(settle_ms_)) {
        return;
    }

    std::string error;
    if (!ensure_spi_node(config_.spi_device, cancel_, error)) {
        publish(Phase::Failed, error);
        return;
    }
    if (cancelled()) {
        return;
    }
    auto chip = St25r3916::open(config_, error);
    if (!chip) {
        publish(Phase::Failed, error);
        return;
    }

    switch (job_) {
        case Job::Read:
            run_read(*chip);
            break;
        case Job::Write:
            run_write(*chip);
            break;
        case Job::Emulate:
            run_emulate(*chip);
            break;
    }
    if (chip->failed()) {
        publish(Phase::Failed, "NFC: " + chip->io_error());
    }
}

void NfcWorker::run_read(St25r3916& chip) {
    const char* const waiting = "Hold a tag against the Cap";
    std::string error;
    if (!chip.field_on(error)) {
        publish(Phase::Failed, error);
        return;
    }
    publish(Phase::Waiting, waiting);

    int errors = 0;
    while (!cancelled() && !chip.failed()) {
        model::NfcTag tag;
        const Activation activation = activate(chip, tag);
        if (activation == Activation::Ok) {
            errors = 0;
            publish(Phase::Working, "Reading " + model::nfc_hex(tag.uid.data(), tag.uid.size(), "") + "...");
            /* SAK 00 is what NTAG and Ultralight tags answer; everything else is kept as UID only. */
            if ((tag.sak & 0x20) != 0) {
                read_ats(chip, tag);
            }
            if (tag.sak != 0x00 || read_type2(chip, tag)) {
                halt(chip);
                publish_tag(tag, "Tag read");
                return;
            }
            publish(Phase::Waiting, "Lost the tag: hold it still");
        }
        else if (activation == Activation::Collision) {
            publish(Phase::Waiting, "Several tags answer: present only one");
        }
        else if (activation == Activation::NoTag) {
            errors = 0;
            /* No NFC-A tag: the same poll asks for FeliCa, NFC-B and NFC-V, then the field goes back to NFC-A. */
            chip.field_off();
            if (!sleep_for(5) || !chip.field_on_nfcf(error)) {
                if (!cancelled()) {
                    publish(Phase::Failed, error);
                }
                return;
            }
            const FelicaRead felica = read_felica(chip, tag);
            if (felica == FelicaRead::Ok) {
                publish_tag(tag, "Tag read");
                return;
            }
            chip.field_off();
            if (!sleep_for(5) || !chip.field_on_nfcb(error)) {
                if (!cancelled()) {
                    publish(Phase::Failed, error);
                }
                return;
            }
            const NfcbRead nfcb = felica == FelicaRead::NoTag ? read_nfcb(chip, tag) : NfcbRead::NoTag;
            if (nfcb == NfcbRead::Ok) {
                publish_tag(tag, "Tag read");
                return;
            }
            NfcvRead nfcv = NfcvRead::NoTag;
            if (nfcb == NfcbRead::NoTag && felica == FelicaRead::NoTag) {
                chip.field_off();
                if (!sleep_for(5) || !chip.field_on_nfcv(error)) {
                    if (!cancelled()) {
                        publish(Phase::Failed, error);
                    }
                    return;
                }
                nfcv = read_nfcv(chip, tag);
                if (nfcv == NfcvRead::Ok) {
                    publish_tag(tag, "Tag read");
                    return;
                }
            }
            if (felica == FelicaRead::Collision || nfcb == NfcbRead::Collision || nfcv == NfcvRead::Collision) {
                publish(Phase::Waiting, "Several tags answer: present only one");
            }
            else if (felica == FelicaRead::Lost || nfcb == NfcbRead::Lost || nfcv == NfcvRead::Lost) {
                publish(Phase::Waiting, "Lost the tag: hold it still");
            }
            else {
                publish(Phase::Waiting, waiting);
            }
            chip.field_off();
            if (!sleep_for(5) || !chip.field_on(error)) {
                if (!cancelled()) {
                    publish(Phase::Failed, error);
                }
                return;
            }
        }
        else if (++errors >= kErrorsBeforeFieldReset) {
            /* A tag that stopped answering sensibly is reset by dropping the field. */
            errors = 0;
            chip.field_off();
            if (!sleep_for(10) || !chip.field_on(error)) {
                if (!cancelled()) {
                    publish(Phase::Failed, error);
                }
                return;
            }
        }
        if (!sleep_for(kTagPollMs)) {
            return;
        }
    }
}

void NfcWorker::run_write(St25r3916& chip) {
    if (!source_.type2 || source_.pages_total <= kFirstUserPage) {
        publish(Phase::Failed, "Only NTAG / Ultralight tags can be written");
        return;
    }
    /* Only user memory that was actually read is written. */
    const uint16_t end = std::min(model::nfc_user_end(source_), source_.pages_read);
    if (end <= kFirstUserPage || source_.data.size() < static_cast<std::size_t>(end) * model::kNfcPageSize) {
        publish(Phase::Failed, "The saved tag holds no user memory to write");
        return;
    }
    const std::string waiting = "Hold a blank " + source_.type_name + " against the Cap";
    std::string error;
    if (!chip.field_on(error)) {
        publish(Phase::Failed, error);
        return;
    }
    publish(Phase::Waiting, waiting);

    while (!cancelled() && !chip.failed()) {
        model::NfcTag target;
        const Activation activation = activate(chip, target);
        if (activation == Activation::Collision) {
            publish(Phase::Waiting, "Several tags answer: present only one");
        }
        else if (activation != Activation::Ok) {
            publish(Phase::Waiting, waiting);
        }
        else if (target.sak != 0x00) {
            publish(Phase::Waiting, "That is not an NTAG / Ultralight tag");
        }
        else {
            publish(Phase::Working, "Checking the tag...");
            std::vector<uint8_t> answer;
            bool present = read_version(chip, target);
            bool same_type = present && target.has_version == source_.has_version;
            if (same_type && source_.has_version) {
                same_type = target.version[2] == source_.version[2] && target.version[6] == source_.version[6];
            }
            else if (same_type) {
                /* No version to compare: the tag must at least have the source's last page. */
                same_type = read_pages(chip, static_cast<uint16_t>(source_.pages_total - 1), answer) ==
                            CommandResult::Ok;
                present = same_type || reactivate(chip, target);
            }

            if (!present) {
                publish(Phase::Waiting, "Lost the tag: hold it still");
            }
            else if (!same_type) {
                publish(Phase::Waiting, "Wrong tag type: a " + source_.type_name + " is needed");
            }
            else {
                publish(Phase::Working, "Writing...");
                uint16_t written = 0;
                uint16_t refused_page = 0;
                bool lost = false;
                for (uint16_t page = kFirstUserPage; page < end && !lost && refused_page == 0; ++page) {
                    const uint8_t* wanted = source_.data.data() + page * model::kNfcPageSize;
                    /* Pages that already hold the right bytes are left alone. */
                    if (read_pages(chip, page, answer) != CommandResult::Ok) {
                        lost = true;
                        break;
                    }
                    if (std::equal(wanted, wanted + model::kNfcPageSize, answer.begin())) {
                        continue;
                    }
                    uint8_t tx[6] = {kCmdWrite, static_cast<uint8_t>(page)};
                    std::copy_n(wanted, model::kNfcPageSize, tx + 2);
                    const CommandResult result = command(chip, tx, sizeof(tx), 0, kWriteTimeoutMs, answer);
                    if (result == CommandResult::Nak) {
                        refused_page = page;
                    }
                    else if (result != CommandResult::Ok) {
                        lost = true;
                    }
                    else {
                        ++written;
                    }
                }

                /* Read everything back before calling it done. */
                uint16_t mismatch_page = 0;
                for (uint16_t page = kFirstUserPage; page < end && !lost && refused_page == 0; ++page) {
                    const uint8_t* wanted = source_.data.data() + page * model::kNfcPageSize;
                    if (read_pages(chip, page, answer) != CommandResult::Ok) {
                        lost = true;
                    }
                    else if (!std::equal(wanted, wanted + model::kNfcPageSize, answer.begin())) {
                        mismatch_page = page;
                        break;
                    }
                }

                if (refused_page != 0) {
                    publish(Phase::Failed,
                            "Page " + std::to_string(refused_page) + " is locked or password protected");
                    return;
                }
                if (mismatch_page != 0) {
                    publish(Phase::Failed, "Page " + std::to_string(mismatch_page) + " did not take the data");
                    return;
                }
                if (!lost) {
                    halt(chip);
                    const int pages = end - kFirstUserPage;
                    publish(Phase::Done,
                            "Written and verified: " + std::to_string(written) + " of " +
                                std::to_string(pages) + " pages changed");
                    return;
                }
                publish(Phase::Waiting, "Lost the tag: hold it still, starting over");
            }
        }
        if (!sleep_for(kTagPollMs)) {
            return;
        }
    }
}

void NfcWorker::run_emulate(St25r3916& chip) {
    std::string error;
    if (!chip.listen_start(source_.uid, source_.atqa, source_.sak, error)) {
        publish(Phase::Failed, error);
        return;
    }
    raise_thread_priority();
    publish(Phase::Waiting, "Waiting for a reader");

    model::NfcType2Emulator emulator(source_);
    const bool type2 = source_.type2 && source_.pages_total > 0;
    /* A card with an ATS that announces ISO 14443-4 (SAK bit 0x20) answers RATS. */
    const bool iso_dep = !type2 && source_.ats.size() >= 2 && (source_.sak & 0x20) != 0;
    model::IsoDepEmulator iso_dep_emulator(iso_dep ? source_.ats : std::vector<uint8_t>{});
    std::vector<uint8_t> frame;
    /* The last command that got no answer, for the status line; negative while there is none. */
    int refused = -1;
    uint32_t seen_fields = 0;
    uint32_t seen_selections = 0;

    while (!cancelled() && !chip.failed()) {
        const bool got_frame = chip.listen_poll(50, frame);

        /* A new field lets the NFC counter count again; a new selection forgets any authentication. */
        const auto& progress = chip.listen_stats();
        if (progress.fields != seen_fields) {
            seen_fields = progress.fields;
            emulator.begin_field();
        }
        if (progress.selections != seen_selections) {
            seen_selections = progress.selections;
            emulator.begin_selection();
            iso_dep_emulator.begin_selection();
        }

        if (got_frame) {
            if (iso_dep) {
                const auto reply = iso_dep_emulator.handle(frame.data(), frame.size());
                if (!reply.data.empty()) {
                    chip.listen_reply(reply.data.data(), reply.data.size());
                }
                if (reply.halt) {
                    /* After DESELECT the answer goes out first: a few bytes take well under a millisecond. */
                    if (!reply.data.empty()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                    chip.listen_end_session(true);
                }
                else if (reply.data.empty()) {
                    chip.listen_end_session(false);
                }
            }
            else if (!type2) {
                /* UID only: the identity is all there is, so nothing after the selection is answered. */
                const bool halted = frame.size() == 2 && frame[0] == kCmdHalt;
                chip.listen_end_session(halted);
                if (!halted && !frame.empty()) {
                    refused = frame[0];
                }
            }
            else {
                const auto reply = emulator.handle(frame.data(), frame.size());
                switch (reply.kind) {
                    case model::NfcType2Emulator::ReplyKind::Data:
                        chip.listen_reply(reply.data.data(), reply.data.size());
                        break;
                    case model::NfcType2Emulator::ReplyKind::Ack:
                        chip.listen_reply_nibble(reply.nibble);
                        break;
                    case model::NfcType2Emulator::ReplyKind::Nak:
                        /* Refused, as a real NTAG does: a four-bit NAK. */
                        chip.listen_reply_nibble(reply.nibble);
                        if (!frame.empty()) {
                            refused = frame[0];
                        }
                        break;
                    case model::NfcType2Emulator::ReplyKind::None:
                        chip.listen_end_session(reply.halt);
                        if (!reply.halt && !frame.empty()) {
                            refused = frame[0];
                        }
                        break;
                }
            }
        }

        /*
         * The status says how far readers get, which is what tells a field
         * problem from a selection problem from a timing problem. It is only
         * updated once the reader has gone quiet: a new status redraws the
         * screen, and doing that between a reader's commands delays the next
         * answer past the reader's timeout.
         */
        if (got_frame) {
            continue;
        }
        const bool field = chip.listen_field_present();
        const auto& stats = chip.listen_stats();
        std::string text = field ? "Reader present" : "Waiting for a reader";
        if (stats.fields != 0 && stats.attempts == 0) {
            text += ": field seen " + std::to_string(stats.fields) + "x, no anticollision";
            if (stats.bitrate > 0) {
                text += " (bit rate " + std::to_string(stats.bitrate) + ")";
            }
        }
        else if (stats.attempts != 0 && stats.selections == 0) {
            text += ": " + std::to_string(stats.attempts) + " anticollision frames, not selected";
        }
        else if (stats.selections != 0) {
            text += ": selected " + std::to_string(stats.selections) + "x, " + std::to_string(stats.frames) +
                    " cmds";
            if (type2) {
                text += ", " + std::to_string(emulator.reads()) + " reads, " +
                        std::to_string(emulator.writes()) + " writes";
            }
            else if (iso_dep) {
                text += ", ATS sent " + std::to_string(iso_dep_emulator.rats()) + "x";
                if (iso_dep_emulator.blocks() != 0) {
                    text += ", " + std::to_string(iso_dep_emulator.blocks()) + " APDUs unanswered";
                }
            }
            if (stats.bad_frames != 0) {
                text += ", " + std::to_string(stats.bad_frames) + " bad";
            }
            if (refused >= 0) {
                char code[8];
                std::snprintf(code, sizeof(code), "%02X", static_cast<unsigned>(refused));
                text += std::string(", refused ") + code;
            }
        }
        publish(field ? Phase::Working : Phase::Waiting, text);
    }
    chip.listen_stop();
}

} // namespace platform
