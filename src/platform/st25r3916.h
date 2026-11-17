/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "gpio_line.h"
#include "spi_device.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace platform {

struct St25r3916Config {
    /* The Cap CC1101's NFC reader: chip select 2 of SPI0, added by a BSP overlay. */
    std::string spi_device{"/dev/spidev0.2"};
    uint32_t spi_speed_hz{5000000};
    std::string gpio_chip{"/dev/gpiochip0"};
    /* The chip's interrupt output. */
    int irq_line{23};
};

/*
 * ST25R3916 NFC transceiver over spidev, for NFC-A (ISO 14443-3A) at
 * 106 kbit/s. Two roles:
 *
 *  - reader: the chip makes the field and exchanges frames with a tag;
 *  - listener: the chip answers a reader's REQA, anticollision and SELECT by
 *    itself from a stored UID, then hands every further frame to the host.
 *
 * The Cap must already be powered (see CapPower). All calls block, the
 * longest for a few hundred milliseconds, so use it from a worker thread.
 *
 * An SPI failure is remembered: every later call does nothing, failed() is
 * true and io_error() says what happened.
 */
class St25r3916 {
public:
    /* Opens SPI and the interrupt line, resets the chip and checks its identity. */
    static std::unique_ptr<St25r3916> open(const St25r3916Config& config, std::string& error);

    /* Field off, listener stopped, interrupts masked. */
    ~St25r3916();
    St25r3916(const St25r3916&) = delete;
    St25r3916& operator=(const St25r3916&) = delete;

    /* The IC identity register: type in the upper five bits, revision in the lower three. */
    uint8_t identity() const;
    /* The chip's supply voltage as it measured it at start-up, in millivolts; 0 if unknown. */
    unsigned int supply_mv() const;
    bool failed() const;
    const std::string& io_error() const;

    /* ---- Reader ---- */

    /*
     * Sets the chip up as an NFC-A reader and turns the field on, including
     * the 5 ms a tag needs before its first command. Fails when another
     * reader's field is already present.
     */
    bool field_on(std::string& error);
    void field_off();

    enum class RxStatus {
        Ok,
        /* Nothing answered. */
        Timeout,
        /* More than one tag answered; `data` holds what was received up to the collision. */
        Collision,
        /* Something answered, but the frame was damaged. */
        Error,
    };

    struct RxFrame {
        /* As received, including a CRC when the tag sent one. */
        std::vector<uint8_t> data;
        /* Valid bits in the last byte; 0 when it is complete. */
        uint8_t last_bits{0};
    };

    /* Sends WUPA and receives the ATQA. */
    RxStatus wake_up(RxFrame& rx);
    /*
     * Sends one frame and receives the answer. With `anticollision` the
     * receiver expects several tags answering at once. The receiver never
     * checks or strips a CRC: the caller does.
     */
    RxStatus exchange(const uint8_t* tx,
                      std::size_t size,
                      bool append_crc,
                      bool anticollision,
                      unsigned int timeout_ms,
                      RxFrame& rx);
    /* Sends a frame nobody answers (HLTA). */
    void transmit(const uint8_t* tx, std::size_t size, bool append_crc);

    /*
     * Sets the chip up as an NFC-V (ISO 15693) reader, 26 kbit/s with one
     * subcarrier, and turns the field on. field_on() switches back to NFC-A.
     */
    bool field_on_nfcv(std::string& error);
    /*
     * Sets the chip up as an NFC-B (ISO 14443-B) reader at 106 kbit/s and
     * turns the field on. The chip frames and adds the CRC_B itself, so
     * exchange() works as for NFC-A; the CRC_B comes back in the frame.
     */
    bool field_on_nfcb(std::string& error);
    /*
     * Sets the chip up as an NFC-F (FeliCa) reader at 212 kbit/s and turns
     * the field on. Frames start with their length byte; the chip adds the
     * CRC, which comes back at the end of the answer.
     */
    bool field_on_nfcf(std::string& error);
    /*
     * Sends one ISO 15693 request (flags, command, parameters; the CRC is
     * added) and receives the answer without its CRC.
     */
    RxStatus exchange_nfcv(const std::vector<uint8_t>& request, unsigned int timeout_ms, std::vector<uint8_t>& answer);

    /* ---- Listener ---- */

    /*
     * Starts answering as a tag with this identity. Only 4- and 7-byte UIDs
     * are possible. The chip sleeps until a reader's field appears.
     */
    bool listen_start(const std::vector<uint8_t>& uid,
                      const std::array<uint8_t, 2>& atqa,
                      uint8_t sak,
                      std::string& error);
    /*
     * Runs the listener for up to `timeout_ms`. Returns true as soon as a
     * selected reader has sent a frame; `frame` then holds it without its CRC,
     * which the chip has checked. Answer with listen_reply() or
     * listen_reply_bits(), or end the exchange with listen_end_session().
     */
    bool listen_poll(unsigned int timeout_ms, std::vector<uint8_t>& frame);
    /* Sends an answer, followed by a CRC. */
    void listen_reply(const uint8_t* data, std::size_t size);
    /* Sends the low four bits of `nibble`, without CRC: a Type 2 acknowledge. */
    void listen_reply_nibble(uint8_t nibble);
    /*
     * No answer to the reader's last frame. After HLTA (`halt`) the tag goes
     * to sleep until a WUPA; otherwise it stays selected for the next command.
     */
    void listen_end_session(bool halt);
    void listen_stop();

    /* How far readers have come with the emulated tag since listen_start(). */
    struct ListenStats {
        /* Times a reader's field appeared. */
        uint32_t fields{0};
        /* Times the chip started answering an anticollision. */
        uint32_t attempts{0};
        /* Times a reader completed the selection. */
        uint32_t selections{0};
        /* Frames handed to the host after a selection, and frames dropped as damaged. */
        uint32_t frames{0};
        uint32_t bad_frames{0};
        /* The bit rate answered: always 0 = 106 kbit/s; negative before listen_start(). */
        int bitrate{-1};
    };

    /* True while a reader's field is present. */
    bool listen_field_present() const;
    const ListenStats& listen_stats() const;

private:
    enum class ListenState {
        Stopped,
        /* The chip answers REQA, anticollision and SELECT by itself. */
        Listening,
        /* Selected: frames go to the host. */
        Active,
    };

    St25r3916(std::unique_ptr<spi::Device> device, std::unique_ptr<gpio::EdgeInput> irq);

    bool initialize(std::string& error);
    bool enable_oscillator();
    /* Field on with collision avoidance, after a reader set-up. */
    bool start_field(std::string& error);
    /* NFC-B and NFC-V change registers NFC-A leaves alone; their NFC-A values are kept here meanwhile. */
    void save_nfca_registers();
    void restore_nfca_registers();

    std::vector<uint8_t> xfer(const std::vector<uint8_t>& tx);
    /* Write-only frames in one go; see spi::Device::write_frames(). */
    void write_frames(const std::vector<std::vector<uint8_t>>& frames);
    /* Clears the FIFO, loads `bytes` whole bytes plus `bits` more, and sends them with `transmit`. */
    void send_reply(const uint8_t* data, std::size_t bytes, uint8_t bits, uint8_t transmit);
    uint8_t read_register(uint8_t reg);
    void write_register(uint8_t reg, uint8_t value);
    void write_registers(uint8_t reg, const uint8_t* values, std::size_t size);
    uint8_t read_register_b(uint8_t reg);
    void write_register_b(uint8_t reg, uint8_t value);
    void modify_register(uint8_t reg, uint8_t set_mask, uint8_t clear_mask);
    void command(uint8_t code);
    void write_interrupt_mask(uint32_t mask);
    void unmask_interrupts(uint32_t bits);
    void mask_interrupts(uint32_t bits);

    void collect_interrupts();
    void clear_interrupts();
    /* Takes the pending interrupts in `mask`, reading the chip first if its interrupt line is high. */
    uint32_t take_interrupts(uint32_t mask);
    uint32_t wait_interrupts(uint32_t mask, unsigned int timeout_us);

    void load_fifo(const uint8_t* data, std::size_t size);
    void set_transmit_length(std::size_t bytes, uint8_t bits);
    void read_fifo(RxFrame& rx);
    RxStatus receive(unsigned int timeout_ms, RxFrame& rx);
    void prepare_exchange(unsigned int timeout_ms, bool anticollision);

    bool external_field();
    /* Back to waiting for REQA, with the chip answering by itself. */
    void listen_arm();
    void listen_field_gone();
    /* Reads the interrupt registers and acts on them. True when `frame` holds a reader command. */
    bool listen_handle_irq(std::vector<uint8_t>& frame);
    bool listen_frame(uint8_t errors, bool just_selected, std::vector<uint8_t>& frame);

    std::unique_ptr<spi::Device> device_;
    std::unique_ptr<gpio::EdgeInput> irq_;
    uint8_t identity_{0};
    unsigned int supply_mv_{0};
    bool failed_{false};
    std::string io_error_;
    /* Interrupts read from the chip and not yet taken: main, timer, error, passive target. */
    uint32_t pending_{0};
    bool field_on_{false};
    /* What the reader is set up for; NFC-B and NFC-V change registers NFC-A leaves alone. */
    enum class FieldMode { A, B, F, V };
    FieldMode field_mode_{FieldMode::A};
    bool nfca_saved_{false};
    std::array<uint8_t, 7> nfca_saved_a_{};
    std::array<uint8_t, 9> nfca_saved_b_{};

    ListenState listen_state_{ListenState::Stopped};
    /* Asleep after HLTA: only a WUPA selects the tag again. */
    bool listen_halted_{false};
    bool listen_field_{false};
    /* A selection happened in the present field, so its end needs re-arming. */
    bool listen_selected_in_field_{false};
    /* Last interrupt or field check, for checking a silent field directly. */
    std::chrono::steady_clock::time_point listen_quiet_since_{};
    ListenStats listen_stats_;
    /* What the interrupt mask registers hold, so changing a bit needs no read. */
    uint32_t interrupt_mask_{0};
};

} // namespace platform
