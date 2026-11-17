/*
 * SPDX-License-Identifier: MIT
 */

#include "protocols/key_factory.h"

#include <cstring>
#include <random>

namespace model {
namespace {

constexpr KeyTemplate kTemplates[] = {
    {"Princeton", 24, 390},
    {"CAME", 12, 0},
    {"CAME", 24, 0},
    {"Nice FLO", 12, 0},
    {"Nice FLO", 24, 0},
    {"Linear", 10, 0},
    {"LinearDelta3", 8, 0},
    {"GateTX", 24, 0},
    {"Holtek", 40, 0},
    {"Ansonic", 12, 0},
    {"SMC5326", 25, 300},
    {"Clemsa", 18, 0},
    {"BETT", 18, 0},
    {"Doitrand", 37, 0},
    {"Dooya", 40, 0},
    {"Phoenix_V2", 52, 0},
    {"Hormann HSM", 44, 0},
    {"Intertechno_V3", 32, 0},
    {"Marantec", 49, 0},
    {"MegaCode", 24, 0},
    {"Cham_Code", 9, 0},
};

constexpr std::size_t kTemplateCount = sizeof(kTemplates) / sizeof(kTemplates[0]);

bool is_protocol(const KeyTemplate& key_template, const char* name) {
    return std::strcmp(key_template.protocol, name) == 0;
}

} // namespace

std::size_t key_template_count() {
    return kTemplateCount;
}

const KeyTemplate& key_template(std::size_t index) {
    return kTemplates[index % kTemplateCount];
}

uint64_t normalize_key(const KeyTemplate& key_template, uint64_t key) {
    const uint64_t mask = key_template.bits >= 64 ? ~0ULL : ((1ULL << key_template.bits) - 1);
    key &= mask;

    if (is_protocol(key_template, "Holtek")) {
        /* The top four bits are a fixed header. */
        key = (key & 0x0FFFFFFFFFULL) | 0x5000000000ULL;
    }
    else if (is_protocol(key_template, "Hormann HSM")) {
        /* The top byte and the lowest two bits are always set. */
        key |= 0xFF000000003ULL;
    }
    else if (is_protocol(key_template, "Marantec") || is_protocol(key_template, "MegaCode")) {
        /* The frame always starts with a 1. */
        key |= 1ULL << (key_template.bits - 1);
    }
    return key;
}

uint64_t random_key(const KeyTemplate& key_template) {
    std::random_device device;
    const uint64_t value = (static_cast<uint64_t>(device()) << 32) ^ device();
    return normalize_key(key_template, value);
}

} // namespace model
