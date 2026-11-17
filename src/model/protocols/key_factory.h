/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace model {

/*
 * The fixed-code remotes that can be created from scratch on the Add Manually
 * page. A created key is a new remote: the receiver has to learn it, through
 * its own learning procedure, before it reacts.
 *
 * Only protocols that KeyEncoder can send and whose key is free to choose are
 * listed. Rolling codes are not.
 */
struct KeyTemplate {
    /* Protocol name as used in .sub files. */
    const char* protocol;
    uint32_t bits;
    /* Pulse length stored with the key, for the protocols that need one; otherwise 0. */
    uint32_t te_us;
};

std::size_t key_template_count();
const KeyTemplate& key_template(std::size_t index);

/*
 * Makes any number a valid key for the template: cut to its length, with the
 * bits a protocol fixes (a header, a leading 1) set the way it needs.
 */
uint64_t normalize_key(const KeyTemplate& key_template, uint64_t key);

/* A new random key for the template. */
uint64_t random_key(const KeyTemplate& key_template);

} // namespace model
