/*
 * SPDX-License-Identifier: MIT
 */

#include "amiibo.h"

#include <algorithm>
#include <cstring>

namespace model {
namespace {

/* ---- SHA-256 and HMAC ------------------------------------------------------- */

constexpr uint32_t kSha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

using Digest = std::array<uint8_t, 32>;

uint32_t rotr(uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
}

void sha256_block(uint32_t state[8], const uint8_t* block) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(block[4 * i]) << 24) | (static_cast<uint32_t>(block[4 * i + 1]) << 16) |
               (static_cast<uint32_t>(block[4 * i + 2]) << 8) | block[4 * i + 3];
    }
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (int i = 0; i < 64; ++i) {
        const uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + kSha256K[i] + w[i];
        const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

Digest sha256(const std::vector<uint8_t>& message) {
    uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                         0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::vector<uint8_t> padded(message);
    const uint64_t bits = static_cast<uint64_t>(message.size()) * 8;
    padded.push_back(0x80);
    while (padded.size() % 64 != 56) {
        padded.push_back(0x00);
    }
    for (int shift = 56; shift >= 0; shift -= 8) {
        padded.push_back(static_cast<uint8_t>(bits >> shift));
    }
    for (std::size_t offset = 0; offset < padded.size(); offset += 64) {
        sha256_block(state, padded.data() + offset);
    }
    Digest digest{};
    for (int i = 0; i < 8; ++i) {
        digest[4 * i] = static_cast<uint8_t>(state[i] >> 24);
        digest[4 * i + 1] = static_cast<uint8_t>(state[i] >> 16);
        digest[4 * i + 2] = static_cast<uint8_t>(state[i] >> 8);
        digest[4 * i + 3] = static_cast<uint8_t>(state[i]);
    }
    return digest;
}

Digest hmac_sha256(const uint8_t* key, std::size_t key_size, const std::vector<uint8_t>& message) {
    /* Keys here are 16 bytes, shorter than the 64-byte block. */
    std::vector<uint8_t> inner(64, 0x36);
    std::vector<uint8_t> outer(64, 0x5c);
    for (std::size_t i = 0; i < key_size && i < 64; ++i) {
        inner[i] ^= key[i];
        outer[i] ^= key[i];
    }
    inner.insert(inner.end(), message.begin(), message.end());
    const Digest inner_digest = sha256(inner);
    outer.insert(outer.end(), inner_digest.begin(), inner_digest.end());
    return sha256(outer);
}

/* ---- AES-128, encryption only (CTR mode needs no more) ---------------------- */

constexpr uint8_t kSbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

uint8_t xtime(uint8_t value) {
    return static_cast<uint8_t>((value << 1) ^ ((value & 0x80) ? 0x1b : 0x00));
}

class Aes128 {
public:
    explicit Aes128(const uint8_t key[16]) {
        std::memcpy(round_keys_, key, 16);
        uint8_t rcon = 0x01;
        for (int i = 16; i < 176; i += 4) {
            uint8_t word[4] = {round_keys_[i - 4], round_keys_[i - 3], round_keys_[i - 2], round_keys_[i - 1]};
            if (i % 16 == 0) {
                const uint8_t first = word[0];
                word[0] = static_cast<uint8_t>(kSbox[word[1]] ^ rcon);
                word[1] = kSbox[word[2]];
                word[2] = kSbox[word[3]];
                word[3] = kSbox[first];
                rcon = xtime(rcon);
            }
            for (int j = 0; j < 4; ++j) {
                round_keys_[i + j] = static_cast<uint8_t>(round_keys_[i - 16 + j] ^ word[j]);
            }
        }
    }

    void encrypt(const uint8_t in[16], uint8_t out[16]) const {
        uint8_t s[16];
        for (int i = 0; i < 16; ++i) {
            s[i] = static_cast<uint8_t>(in[i] ^ round_keys_[i]);
        }
        for (int round = 1; round <= 10; ++round) {
            uint8_t t[16];
            /* SubBytes and ShiftRows; the state is column-major. */
            for (int column = 0; column < 4; ++column) {
                for (int row = 0; row < 4; ++row) {
                    t[4 * column + row] = kSbox[s[4 * ((column + row) % 4) + row]];
                }
            }
            if (round != 10) {
                for (int column = 0; column < 4; ++column) {
                    uint8_t* c = t + 4 * column;
                    const uint8_t all = static_cast<uint8_t>(c[0] ^ c[1] ^ c[2] ^ c[3]);
                    const uint8_t first = c[0];
                    c[0] = static_cast<uint8_t>(c[0] ^ all ^ xtime(static_cast<uint8_t>(c[0] ^ c[1])));
                    c[1] = static_cast<uint8_t>(c[1] ^ all ^ xtime(static_cast<uint8_t>(c[1] ^ c[2])));
                    c[2] = static_cast<uint8_t>(c[2] ^ all ^ xtime(static_cast<uint8_t>(c[2] ^ c[3])));
                    c[3] = static_cast<uint8_t>(c[3] ^ all ^ xtime(static_cast<uint8_t>(c[3] ^ first)));
                }
            }
            for (int i = 0; i < 16; ++i) {
                s[i] = static_cast<uint8_t>(t[i] ^ round_keys_[16 * round + i]);
            }
        }
        std::memcpy(out, s, 16);
    }

private:
    uint8_t round_keys_[176];
};

/* AES-128-CTR with a 16-byte big-endian counter block, in place. */
void aes_ctr(const uint8_t key[16], const uint8_t iv[16], uint8_t* data, std::size_t size) {
    const Aes128 aes(key);
    uint8_t counter[16];
    std::memcpy(counter, iv, 16);
    uint8_t stream[16];
    for (std::size_t offset = 0; offset < size; offset += 16) {
        aes.encrypt(counter, stream);
        for (std::size_t i = 0; i < 16 && offset + i < size; ++i) {
            data[offset + i] ^= stream[i];
        }
        for (int i = 15; i >= 0; --i) {
            if (++counter[i] != 0) {
                break;
            }
        }
    }
}

/* ---- Amiibo layout (amiitool) ----------------------------------------------- */

constexpr std::size_t kInternalSize = 520;
constexpr std::size_t kHmacPosData = 0x008;
constexpr std::size_t kHmacPosTag = 0x1B4;
constexpr std::size_t kCipherStart = 0x02C;
constexpr std::size_t kCipherEnd = kCipherStart + 0x188;

using Bytes = std::vector<uint8_t>;

/* Moves `size` bytes from `from` at `source` to `to` at `target`. */
void move_bytes(const Bytes& from, std::size_t source, Bytes& to, std::size_t target, std::size_t size) {
    std::copy_n(from.begin() + static_cast<std::ptrdiff_t>(source), size,
                to.begin() + static_cast<std::ptrdiff_t>(target));
}

/* The tag order of the 540 bytes, as amiitool rearranges them: {tag offset, internal offset, size}. */
struct Span {
    std::size_t tag;
    std::size_t internal;
    std::size_t size;
};
constexpr Span kLayout[] = {
    {0x008, 0x000, 0x008}, {0x080, 0x008, 0x020}, {0x010, 0x028, 0x024}, {0x0A0, 0x04C, 0x168},
    {0x034, 0x1B4, 0x020}, {0x000, 0x1D4, 0x008}, {0x054, 0x1DC, 0x02C},
};

Bytes tag_to_internal(const Bytes& tag) {
    Bytes internal(kInternalSize, 0);
    for (const auto& span : kLayout) {
        move_bytes(tag, span.tag, internal, span.internal, span.size);
    }
    return internal;
}

Bytes internal_to_tag(const Bytes& internal, const Bytes& template_tag) {
    Bytes tag(template_tag);
    for (const auto& span : kLayout) {
        move_bytes(internal, span.internal, tag, span.tag, span.size);
    }
    return tag;
}

struct DerivedKeys {
    std::array<uint8_t, 16> aes_key{};
    std::array<uint8_t, 16> aes_iv{};
    std::array<uint8_t, 16> hmac_key{};
};

DerivedKeys derive_keys(const AmiiboMasterKey& master, const Bytes& internal) {
    /* Seed: two bytes of settings, the UID pages twice and the 32-byte salt. */
    Bytes seed(64, 0);
    move_bytes(internal, 0x029, seed, 0x00, 2);
    move_bytes(internal, 0x1D4, seed, 0x10, 8);
    move_bytes(internal, 0x1D4, seed, 0x18, 8);
    move_bytes(internal, 0x1E8, seed, 0x20, 32);

    const std::size_t leading = 16 - master.magic_bytes.size();
    Bytes prepared(master.type_string);
    prepared.insert(prepared.end(), seed.begin(), seed.begin() + static_cast<std::ptrdiff_t>(leading));
    prepared.insert(prepared.end(), master.magic_bytes.begin(), master.magic_bytes.end());
    prepared.insert(prepared.end(), seed.begin() + 0x10, seed.begin() + 0x20);
    for (std::size_t i = 0; i < 32; ++i) {
        prepared.push_back(static_cast<uint8_t>(seed[0x20 + i] ^ master.xor_pad[i]));
    }

    Bytes derived;
    for (uint16_t counter = 0; counter < 2; ++counter) {
        Bytes message{static_cast<uint8_t>(counter >> 8), static_cast<uint8_t>(counter)};
        message.insert(message.end(), prepared.begin(), prepared.end());
        const Digest digest = hmac_sha256(master.hmac_key.data(), master.hmac_key.size(), message);
        derived.insert(derived.end(), digest.begin(), digest.end());
    }
    DerivedKeys keys;
    std::copy_n(derived.begin(), 16, keys.aes_key.begin());
    std::copy_n(derived.begin() + 16, 16, keys.aes_iv.begin());
    std::copy_n(derived.begin() + 32, 16, keys.hmac_key.begin());
    return keys;
}

Bytes slice(const Bytes& bytes, std::size_t from, std::size_t to) {
    return Bytes(bytes.begin() + static_cast<std::ptrdiff_t>(from), bytes.begin() + static_cast<std::ptrdiff_t>(to));
}

/* Decrypts the tag's amiibo data and checks both signatures. */
bool unpack(const Bytes& tag, const AmiiboKeys& keys, Bytes& plain, std::string& error) {
    const Bytes internal = tag_to_internal(tag);
    const DerivedKeys data_keys = derive_keys(keys.data, internal);
    const DerivedKeys tag_keys = derive_keys(keys.tag, internal);

    plain.assign(kInternalSize, 0);
    move_bytes(internal, 0x000, plain, 0x000, 0x008);
    move_bytes(internal, 0x028, plain, 0x028, 0x004);
    move_bytes(internal, kCipherStart, plain, kCipherStart, kCipherEnd - kCipherStart);
    aes_ctr(data_keys.aes_key.data(), data_keys.aes_iv.data(), plain.data() + kCipherStart, kCipherEnd - kCipherStart);
    move_bytes(internal, 0x1D4, plain, 0x1D4, 0x208 - 0x1D4);

    const Digest tag_signature = hmac_sha256(tag_keys.hmac_key.data(), 16, slice(plain, 0x1D4, 0x208));
    std::copy(tag_signature.begin(), tag_signature.end(), plain.begin() + kHmacPosTag);
    const Digest data_signature = hmac_sha256(data_keys.hmac_key.data(), 16, slice(plain, 0x029, 0x208));
    std::copy(data_signature.begin(), data_signature.end(), plain.begin() + kHmacPosData);

    if (!std::equal(data_signature.begin(), data_signature.end(), internal.begin() + kHmacPosData)) {
        error = "the amiibo data signature does not match the key";
        return false;
    }
    if (!std::equal(tag_signature.begin(), tag_signature.end(), internal.begin() + kHmacPosTag)) {
        error = "the amiibo tag signature does not match the key";
        return false;
    }
    return true;
}

/* Encrypts and signs `plain` back into the tag order, over `template_tag`. */
Bytes pack(const Bytes& plain, const Bytes& template_tag, const AmiiboKeys& keys) {
    const DerivedKeys data_keys = derive_keys(keys.data, plain);
    const DerivedKeys tag_keys = derive_keys(keys.tag, plain);

    const Digest tag_signature = hmac_sha256(tag_keys.hmac_key.data(), 16, slice(plain, 0x1D4, 0x208));
    Bytes data_material = slice(plain, 0x029, 0x1B4);
    data_material.insert(data_material.end(), tag_signature.begin(), tag_signature.end());
    const Bytes uid_and_salt = slice(plain, 0x1D4, 0x208);
    data_material.insert(data_material.end(), uid_and_salt.begin(), uid_and_salt.end());
    const Digest data_signature = hmac_sha256(data_keys.hmac_key.data(), 16, data_material);

    Bytes internal(kInternalSize, 0);
    std::copy(data_signature.begin(), data_signature.end(), internal.begin() + kHmacPosData);
    std::copy(tag_signature.begin(), tag_signature.end(), internal.begin() + kHmacPosTag);
    move_bytes(plain, 0x000, internal, 0x000, 0x008);
    move_bytes(plain, 0x028, internal, 0x028, 0x004);
    move_bytes(plain, kCipherStart, internal, kCipherStart, kCipherEnd - kCipherStart);
    aes_ctr(data_keys.aes_key.data(), data_keys.aes_iv.data(), internal.data() + kCipherStart,
            kCipherEnd - kCipherStart);
    move_bytes(plain, 0x1D4, internal, 0x1D4, 0x208 - 0x1D4);
    return internal_to_tag(internal, template_tag);
}

bool parse_master_key(const uint8_t* raw, AmiiboMasterKey& key, std::string& error) {
    const std::size_t magic_size = raw[31];
    if (magic_size > 16) {
        error = "key_retail.bin is not a retail key (bad magic size)";
        return false;
    }
    const uint8_t* type_field = raw + 16;
    const uint8_t* terminator = std::find(type_field, type_field + 14, 0);
    if (terminator == type_field + 14) {
        error = "key_retail.bin is not a retail key (no type string)";
        return false;
    }
    std::copy_n(raw, 16, key.hmac_key.begin());
    key.type_string.assign(type_field, terminator + 1);
    key.magic_bytes.assign(raw + 32, raw + 32 + magic_size);
    std::copy_n(raw + 48, 32, key.xor_pad.begin());
    return true;
}

} // namespace

bool amiibo_parse_keys(const std::vector<uint8_t>& raw, AmiiboKeys& keys, std::string& error) {
    error.clear();
    if (raw.size() != kAmiiboKeyFileSize) {
        error = "key_retail.bin must be 160 bytes, not " + std::to_string(raw.size());
        return false;
    }
    return parse_master_key(raw.data(), keys.data, error) && parse_master_key(raw.data() + 80, keys.tag, error);
}

bool amiibo_shaped(const NfcTag& tag) {
    return tag.type2 && tag.pages_total == kAmiiboDumpSize / kNfcPageSize && tag.data.size() >= kAmiiboDumpSize &&
           tag.uid.size() == 7;
}

bool amiibo_verify(const NfcTag& tag, const AmiiboKeys& keys, std::string& error) {
    error.clear();
    if (!amiibo_shaped(tag)) {
        error = "not an NTAG215 with all 540 bytes";
        return false;
    }
    Bytes plain;
    return unpack(Bytes(tag.data.begin(), tag.data.begin() + kAmiiboDumpSize), keys, plain, error);
}

bool amiibo_with_uid(const NfcTag& tag,
                     const AmiiboKeys& keys,
                     const std::array<uint8_t, 7>& uid,
                     const std::array<uint8_t, 32>& salt,
                     NfcTag& out,
                     std::string& error) {
    error.clear();
    if (!amiibo_shaped(tag)) {
        error = "not an NTAG215 with all 540 bytes";
        return false;
    }
    if (uid[0] != 0x04) {
        error = "an amiibo UID starts with 04";
        return false;
    }
    const Bytes original(tag.data.begin(), tag.data.begin() + kAmiiboDumpSize);
    Bytes plain;
    if (!unpack(original, keys, plain, error)) {
        return false;
    }

    /* The new UID with both check bytes, and a fresh salt for the key derivation. */
    plain[0x000] = static_cast<uint8_t>(uid[3] ^ uid[4] ^ uid[5] ^ uid[6]);
    std::copy_n(uid.begin(), 3, plain.begin() + 0x1D4);
    plain[0x1D7] = static_cast<uint8_t>(0x88 ^ uid[0] ^ uid[1] ^ uid[2]);
    std::copy_n(uid.begin() + 3, 4, plain.begin() + 0x1D8);
    std::copy(salt.begin(), salt.end(), plain.begin() + 0x1E8);

    Bytes dump = pack(plain, original, keys);

    /* The password a console sends follows from the UID (page 133); the PACK stays 80 80. */
    const std::size_t password = 133 * kNfcPageSize;
    dump[password] = static_cast<uint8_t>(0xAA ^ uid[1] ^ uid[3]);
    dump[password + 1] = static_cast<uint8_t>(0x55 ^ uid[2] ^ uid[4]);
    dump[password + 2] = static_cast<uint8_t>(0xAA ^ uid[3] ^ uid[5]);
    dump[password + 3] = static_cast<uint8_t>(0x55 ^ uid[4] ^ uid[6]);

    /* Check the result the way a console will. */
    Bytes check;
    if (!unpack(dump, keys, check, error)) {
        error = "re-signing failed: " + error;
        return false;
    }

    out = tag;
    std::copy(dump.begin(), dump.end(), out.data.begin());
    out.uid.assign(uid.begin(), uid.end());
    /* The NXP signature belongs to the original chip; a new UID cannot carry it. */
    out.signature.fill(0);
    out.has_signature = false;
    return true;
}

} // namespace model
