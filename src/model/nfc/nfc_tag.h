/*
 * SPDX-License-Identifier: MIT
 *
 * NFC-A (ISO 14443-3A) tags as the NFC tool sees them: the identity every tag
 * has (UID, ATQA, SAK) and, for NTAG / MIFARE Ultralight tags (NFC Forum
 * Type 2), the memory. Pure logic, no hardware access.
 */

#pragma once

#include "felica.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace model {

/* Bytes in one NTAG / Ultralight page. */
constexpr std::size_t kNfcPageSize = 4;

/* One member of the NTAG / Ultralight family. */
struct NfcType2Spec {
    /* The name Flipper Zero uses in .nfc files, e.g. "NTAG215". */
    const char* name;
    /* Product type and storage size bytes of the GET_VERSION answer; 0 when the tag has no GET_VERSION. */
    uint8_t product_type;
    uint8_t storage_size;
    uint16_t pages_total;
    /* First page after the user memory: lock, configuration and password pages start here. */
    uint16_t user_end;
};

/* The spec matching a GET_VERSION answer, or nullptr for a product this app does not know. */
const NfcType2Spec* nfc_type2_spec_for_version(const std::array<uint8_t, 8>& version);
/* The spec with this Flipper name, or nullptr. */
const NfcType2Spec* nfc_type2_spec_by_name(const std::string& name);
/* A tag without GET_VERSION is told apart by its size alone. */
const char* nfc_type2_name_for_pages(uint16_t pages_total);

/* The radio standard a tag speaks. */
enum class NfcTechnology : uint8_t {
    /* ISO 14443-A: NTAG, MIFARE, DESFire, bank cards. */
    A,
    /* ISO 15693 (vicinity): ICODE SLIX, Tag-it, library and ski tags. */
    V,
    /* ISO 14443-B: Type B cards, and ST25TB / SRIX memory tags on the same radio layer. */
    B,
    /* FeliCa (JIS X 6319-4): Japanese transit and payment cards, FeliCa Lite tags. */
    F,
};

/* One ST25TB / SRIX product, told apart by its UID. */
struct St25tbSpec {
    /* The name Flipper Zero uses in .nfc files, e.g. "4K". */
    const char* type_name;
    const char* full_name;
    uint16_t blocks;
};

/* The product of an ST25TB UID (8 bytes, D0 first), or nullptr. */
const St25tbSpec* nfc_st25tb_spec_for_uid(const std::vector<uint8_t>& uid);
/* The product with this Flipper name, or nullptr. */
const St25tbSpec* nfc_st25tb_spec_by_name(const std::string& name);

struct NfcTag {
    NfcTechnology technology{NfcTechnology::A};
    /*
     * NFC-A: 4, 7 or 10 bytes, in the order they are printed on a reader.
     * NFC-V: 8 bytes, E0 first. ISO 14443-3B: the 4-byte PUPI. ST25TB: 8 bytes, D0 first.
     */
    std::vector<uint8_t> uid;
    /* As received: atqa[0] is the first (low) byte on the air. */
    std::array<uint8_t, 2> atqa{};
    uint8_t sak{0};

    /* True when the tag is an NTAG / Ultralight and the fields below are filled. */
    bool type2{false};
    std::string type_name;
    bool has_version{false};
    std::array<uint8_t, 8> version{};
    bool has_signature{false};
    std::array<uint8_t, 32> signature{};
    uint16_t pages_total{0};
    /* Pages that could be read; fewer than the total on a password-protected tag. */
    uint16_t pages_read{0};
    /* pages_total * 4 bytes; pages that could not be read are zero. */
    std::vector<uint8_t> data;
    /* NTAG21x / Ultralight EV1 one-way counters (24 bits) and their tearing flags; 0xBD is "valid". */
    std::array<uint32_t, 3> counters{};
    std::array<uint8_t, 3> tearing{{0xBD, 0xBD, 0xBD}};

    /* ISO 14443-4A cards: the answer to RATS, starting with its length byte TL; empty otherwise. */
    std::vector<uint8_t> ats;

    /* NFC-V: what GET SYSTEM INFORMATION reports, the memory blocks in `data`, and each block's lock. */
    bool has_dsfid{false};
    uint8_t dsfid{0};
    bool has_afi{false};
    uint8_t afi{0};
    bool has_ic_ref{false};
    uint8_t ic_ref{0};
    bool lock_dsfid{false};
    bool lock_afi{false};
    uint16_t block_count{0};
    uint8_t block_size{0};
    /* Blocks that could be read; the others are zero in `data`. */
    uint16_t blocks_read{0};
    /* One byte per block: 01 locked, 00 not. */
    std::vector<uint8_t> block_security;

    /* ISO 14443-3B: the rest of the answer to REQB. */
    std::array<uint8_t, 4> app_data{};
    std::array<uint8_t, 3> protocol_info{};
    /* ST25TB / SRIX: the product (Flipper's name), the blocks in `data` (4 bytes each), and block 255. */
    bool st25tb{false};
    std::string st25tb_type;
    std::array<uint8_t, 4> st25tb_system{};

    /*
     * FeliCa: the IDm is `uid`. A Lite keeps its 28 blocks in `data` (18 bytes
     * each: two status flags, then the block) with `blocks_read`; a Standard
     * card keeps what it lists in `felica_systems`.
     */
    std::array<uint8_t, 8> felica_pmm{};
    bool felica_lite{false};
    std::vector<FelicaSystem> felica_systems;
};

/* True for an Ultralight C, which authenticates with 3DES instead of a password. */
bool nfc_is_ultralight_c(const NfcTag& tag);

/* First page after the user memory of this tag (see NfcType2Spec::user_end). */
uint16_t nfc_user_end(const NfcTag& tag);

/* What kind of card a tag is, judged by SAK: "NTAG / Ultralight", "MIFARE Classic 1K", ... */
std::string nfc_family_name(const NfcTag& tag);

/* "04 85 92 8A", or "0485928A" with an empty separator. */
std::string nfc_hex(const uint8_t* bytes, std::size_t size, const char* separator = " ");

/* ISO 14443-3A CRC. Appended low byte first; a frame with a correct CRC gives 0 over all its bytes. */
uint16_t nfc_crc_a(const uint8_t* bytes, std::size_t size);

/* Block check character of four UID (or cascade) bytes. */
uint8_t nfc_bcc(const uint8_t* bytes);

/*
 * The cascade levels of a UID as sent during anticollision: one five-byte
 * group per level (cascade tag where needed, UID bytes, BCC). Returns the
 * number of levels, or 0 for a UID that is not 4, 7 or 10 bytes.
 */
std::size_t nfc_cascade_levels(const std::vector<uint8_t>& uid, std::array<std::array<uint8_t, 5>, 3>& levels);

/*
 * Answers the commands a reader sends to a selected NTAG / Ultralight tag,
 * from a copy of the tag's memory. The chip handles everything up to the
 * selection (REQA, anticollision, SELECT) by itself.
 */
class NfcType2Emulator {
public:
    enum class ReplyKind {
        /* Stay silent. */
        None,
        /* Send `data`, followed by a CRC. */
        Data,
        /* Send the four-bit acknowledge in `nibble`, without CRC. */
        Ack,
        /* Send the four-bit refusal in `nibble`; the tag then waits for a new selection. */
        Nak,
    };

    struct Reply {
        ReplyKind kind{ReplyKind::None};
        std::vector<uint8_t> data;
        uint8_t nibble{0};
        /* The reader put the tag to sleep (HLTA). */
        bool halt{false};
    };

    /* Longest answer the transceiver can send in one frame. */
    static constexpr std::size_t kMaxReplyBytes = 480;

    explicit NfcType2Emulator(NfcTag tag);

    /* A reader's field appeared: the NFC counter may count again. */
    void begin_field();
    /* The tag was selected: any authentication from before is gone. */
    void begin_selection();

    /* Handles one frame, given without its CRC. */
    Reply handle(const uint8_t* frame, std::size_t size);

    const NfcTag& tag() const;
    uint32_t reads() const;
    uint32_t writes() const;

private:
    enum class Kind {
        /* Original Ultralight, NTAG203, and anything without GET_VERSION. */
        Plain,
        /* NTAG213 / 215 / 216: one NFC counter (number 2), counted on reads. */
        Ntag21x,
        /* Ultralight EV1: three counters the reader increments. NTAG210 / 212 also land here, without counters. */
        Ev1,
        UltralightC,
    };

    Reply nak(uint8_t code) const;
    Reply read_pages(std::size_t first, std::size_t count);
    /* The ACCESS byte of the configuration (NTAG21x / EV1), or 0. */
    uint8_t access() const;
    bool page_hidden(std::size_t page) const;
    bool ulc_allowed(std::size_t page, bool write) const;
    Reply ulc_auth_start();
    Reply ulc_auth_finish(const uint8_t* frame, std::size_t size);

    NfcTag tag_;
    Kind kind_{Kind::Plain};
    uint32_t reads_{0};
    uint32_t writes_{0};
    bool authenticated_{false};
    bool counted_this_field_{false};
    /* Ultralight C authentication in progress: RndB and the block sent for it. */
    bool ulc_pending_{false};
    std::array<uint8_t, 8> ulc_rnd_b_{};
    std::array<uint8_t, 8> ulc_enc_b_{};
};

/*
 * ISO 14443-4A (ISO-DEP) as far as Flipper Zero's generic emulation goes:
 * RATS is answered with the saved ATS, PPS is accepted, DESELECT puts the
 * card to sleep and R-blocks get a NAK. Application data (I-blocks) gets no
 * answer, since the card's applications are not part of a dump.
 */
class IsoDepEmulator {
public:
    struct Reply {
        /* Empty: stay silent. Otherwise sent followed by a CRC. */
        std::vector<uint8_t> data;
        /* The tag goes to sleep after this (HLTA, DESELECT). */
        bool halt{false};
    };

    explicit IsoDepEmulator(std::vector<uint8_t> ats);

    /* The tag was selected: back to waiting for RATS. */
    void begin_selection();
    Reply handle(const uint8_t* frame, std::size_t size);
    /* Times RATS was answered, and application blocks received. */
    uint32_t rats() const;
    uint32_t blocks() const;

private:
    std::vector<uint8_t> ats_;
    bool active_{false};
    bool can_pps_{false};
    int cid_{-1};
    uint32_t rats_{0};
    uint32_t blocks_{0};
};

} // namespace model
