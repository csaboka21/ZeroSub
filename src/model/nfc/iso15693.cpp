/*
 * SPDX-License-Identifier: MIT
 */

#include "iso15693.h"

#include <algorithm>

namespace model {
namespace {

/* Start and end of a request, and the four 1-out-of-4 symbols, as FIFO bytes (sent LSB first). */
constexpr uint8_t kSof = 0x21;
constexpr uint8_t kEof = 0x04;
constexpr uint8_t kSymbols[4] = {0x02, 0x08, 0x20, 0x80};

/* An answer starts with a fixed five-bit start of frame. */
constexpr uint8_t kResponseSof = 0x17;

int bit_at(const uint8_t* bytes, std::size_t position) {
    return (bytes[position / 8] >> (position % 8)) & 0x01;
}

} // namespace

uint16_t iso15693_crc(const uint8_t* bytes, std::size_t size) {
    uint16_t crc = 0xFFFF;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= bytes[index];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x0001) != 0 ? static_cast<uint16_t>((crc >> 1) ^ 0x8408) : static_cast<uint16_t>(crc >> 1);
        }
    }
    return static_cast<uint16_t>(~crc);
}

std::vector<uint8_t> iso15693_encode_request(const std::vector<uint8_t>& request) {
    std::vector<uint8_t> frame(request);
    const uint16_t crc = iso15693_crc(frame.data(), frame.size());
    frame.push_back(static_cast<uint8_t>(crc));
    frame.push_back(static_cast<uint8_t>(crc >> 8));

    std::vector<uint8_t> coded;
    coded.reserve(2 + 4 * frame.size());
    coded.push_back(kSof);
    for (uint8_t byte : frame) {
        /* Two bits at a time, lowest first, each as one of four pulse positions. */
        for (int pair = 0; pair < 4; ++pair) {
            coded.push_back(kSymbols[byte & 0x03]);
            byte = static_cast<uint8_t>(byte >> 2);
        }
    }
    coded.push_back(kEof);
    return coded;
}

Iso15693Decode iso15693_decode_response(const uint8_t* fifo, std::size_t size, std::vector<uint8_t>& answer) {
    answer.clear();
    if (!fifo || size == 0 || (fifo[0] & 0x1F) != kResponseSof) {
        return Iso15693Decode::Error;
    }

    /* Every data bit is a pair of half bits: 10 is a 0, 01 is a 1 (as read LSB first). */
    const std::size_t half_bits = size * 8;
    const std::size_t max_bits = half_bits > 5 ? (half_bits - 5) / 2 : 0;
    std::vector<uint8_t> out((max_bits + 7) / 8, 0);
    if (out.empty()) {
        return Iso15693Decode::Error;
    }

    std::size_t bits = 0;
    for (std::size_t position = 5; position + 2 < half_bits; position += 2) {
        const int pair = bit_at(fifo, position) | (bit_at(fifo, position + 1) << 1);
        if (pair == 1) {
            ++bits;
        }
        else if (pair == 2) {
            if (bits / 8 < out.size()) {
                out[bits / 8] = static_cast<uint8_t>(out[bits / 8] | (1U << (bits % 8)));
            }
            ++bits;
        }

        /* The end of frame shows up at a byte boundary as the bits 101 then 11000000. */
        bool end = false;
        if (bits % 8 == 0) {
            const std::size_t byte = position / 8;
            if (byte + 1 < size && (fifo[byte] & 0xE0) == 0xA0 && fifo[byte + 1] == 0x03) {
                end = true;
            }
        }
        /* 00 or 11 where a bit belongs: two tags answered with different bits. */
        if ((pair == 0 || pair == 3) && !end) {
            return Iso15693Decode::Collision;
        }
        if (end || bits >= out.size() * 8) {
            break;
        }
    }

    if (bits % 8 != 0 || bits / 8 <= 2) {
        return Iso15693Decode::Error;
    }
    out.resize(bits / 8);
    const uint16_t crc = iso15693_crc(out.data(), out.size() - 2);
    const uint16_t received = static_cast<uint16_t>(out[out.size() - 2] | (out[out.size() - 1] << 8));
    if (crc != received) {
        return Iso15693Decode::Error;
    }
    out.resize(out.size() - 2);
    answer = std::move(out);
    return Iso15693Decode::Ok;
}

std::vector<uint8_t> iso15693_uid_from_air(const uint8_t* air) {
    return std::vector<uint8_t>(std::reverse_iterator<const uint8_t*>(air + 8), std::reverse_iterator<const uint8_t*>(air));
}

std::vector<uint8_t> iso15693_uid_to_air(const std::vector<uint8_t>& uid) {
    return std::vector<uint8_t>(uid.rbegin(), uid.rend());
}

std::string iso15693_type_name(const std::vector<uint8_t>& uid) {
    if (uid.size() != 8 || uid[0] != 0xE0) {
        return "ISO 15693";
    }
    /* UID: E0, manufacturer, then the manufacturer's own type and serial. */
    const uint8_t maker = uid[1];
    const uint8_t type = uid[2];
    switch (maker) {
        case 0x04:
            switch (type) {
                case 0x01:
                    /* Bits 4-3 of the next byte tell ICODE SLI from SLIX and SLIX2. */
                    switch ((uid[3] >> 3) & 0x03) {
                        case 0x02:
                            return "NXP ICODE SLIX";
                        case 0x01:
                            return "NXP ICODE SLIX2";
                        default:
                            return "NXP ICODE SLI";
                    }
                case 0x02:
                    return "NXP ICODE SLIX-S";
                case 0x03:
                    return "NXP ICODE SLIX-L";
                default:
                    return "NXP ICODE";
            }
        case 0x07:
            return "TI Tag-it HF-I";
        case 0x02:
            return "ST ISO 15693";
        case 0x05:
            return "Infineon ISO 15693";
        case 0x08:
            return "Fujitsu ISO 15693";
        case 0x16:
            return "EM Microelectronic ISO 15693";
        default:
            return "ISO 15693";
    }
}

} // namespace model
