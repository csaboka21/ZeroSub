/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "nfc/nfc_tag.h"
#include "st25r3916.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace platform {

/*
 * One NFC job on its own thread: reading a tag, writing a saved tag to
 * another one, or emulating a saved tag. The thread owns the ST25R3916 for
 * as long as the job runs; destroying the worker stops the job, which turns
 * the field off.
 *
 * The UI polls revision() and takes a status() copy when it has changed.
 */
class NfcWorker {
public:
    enum class Job {
        /* Waits for a tag, reads it, and finishes with the tag in the status. */
        Read,
        /* Waits for an NTAG / Ultralight of the same type and writes the user memory of `source` to it. */
        Write,
        /* Answers readers as `source` until stopped. */
        Emulate,
    };

    enum class Phase {
        /* Powering up and opening the chip. */
        Starting,
        /* Waiting for a tag, or for a reader. */
        Waiting,
        /* Talking to a tag. */
        Working,
        /* Finished; the thread has ended. */
        Done,
        Failed,
    };

    struct Status {
        Phase phase{Phase::Starting};
        /* One line for the page. */
        std::string text;
        /* Read: the tag, once the phase is Done. */
        model::NfcTag tag;
    };

    /*
     * Starts the job. `settle_ms` is waited first, for a Cap that has only
     * just been powered. `source` is the tag to write or emulate.
     */
    static std::unique_ptr<NfcWorker> start(const St25r3916Config& config,
                                            Job job,
                                            const model::NfcTag& source,
                                            unsigned int settle_ms);

    ~NfcWorker();
    NfcWorker(const NfcWorker&) = delete;
    NfcWorker& operator=(const NfcWorker&) = delete;

    Job job() const;
    /* Changes whenever the status does. */
    uint32_t revision() const;
    Status status() const;
    /* False once the phase is Done or Failed. */
    bool running() const;

private:
    NfcWorker(const St25r3916Config& config, Job job, const model::NfcTag& source, unsigned int settle_ms);

    void run();
    void run_read(St25r3916& chip);
    void run_write(St25r3916& chip);
    void run_emulate(St25r3916& chip);
    void publish(Phase phase, const std::string& text);
    void publish_tag(const model::NfcTag& tag, const std::string& text);
    bool cancelled() const;
    /* Returns false if the job was cancelled meanwhile. */
    bool sleep_for(unsigned int duration_ms) const;

    const St25r3916Config config_;
    const Job job_;
    const model::NfcTag source_;
    const unsigned int settle_ms_;

    std::atomic<bool> cancel_{false};
    std::atomic<bool> running_{true};
    std::atomic<uint32_t> revision_{0};
    mutable std::mutex mutex_;
    Status status_;
    std::thread thread_;
};

} // namespace platform
