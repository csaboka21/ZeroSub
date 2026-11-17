/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <memory>
#include <string>

namespace platform::gpio {

/*
 * Owning handle for one GPIO output line, requested through the Linux GPIO
 * character device. Device builds only: on desktop builds request() always
 * fails with an explicit "unsupported" error.
 *
 * Releasing the handle does not change the pin: it keeps its last level.
 */
class OutputLine {
public:
    static std::unique_ptr<OutputLine> request(const std::string& chip_path,
                                               unsigned int line_offset,
                                               bool initial_value,
                                               std::string& error);

    ~OutputLine();
    OutputLine(const OutputLine&) = delete;
    OutputLine& operator=(const OutputLine&) = delete;

    bool set_value(bool active, std::string& error);

private:
    explicit OutputLine(int fd);

    int fd_{-1};
};

/*
 * Owning handle for one plain GPIO input line, read on demand. Device builds
 * only, like OutputLine.
 */
class InputLine {
public:
    static std::unique_ptr<InputLine> request(const std::string& chip_path,
                                              unsigned int line_offset,
                                              std::string& error);

    ~InputLine();
    InputLine(const InputLine&) = delete;
    InputLine& operator=(const InputLine&) = delete;

    /* Current level of the line. False when it cannot be read. */
    bool active() const;

private:
    explicit InputLine(int fd);

    int fd_{-1};
};

/*
 * Owning handle for one GPIO input line that reports rising edges, for a
 * chip's interrupt output. Device builds only, like OutputLine.
 */
class EdgeInput {
public:
    static std::unique_ptr<EdgeInput> request(const std::string& chip_path,
                                              unsigned int line_offset,
                                              std::string& error);

    ~EdgeInput();
    EdgeInput(const EdgeInput&) = delete;
    EdgeInput& operator=(const EdgeInput&) = delete;

    /* Current level of the line. False when it cannot be read. */
    bool active() const;
    /*
     * Waits up to `timeout_us` for a rising edge and forgets the edges seen so
     * far. Returns true if at least one arrived.
     */
    bool wait(unsigned int timeout_us);

private:
    explicit EdgeInput(int fd);

    int fd_{-1};
};

/*
 * Remembers how a pin is muxed so it can be put back after the app has used it
 * as a plain GPIO. Uses the Raspberry Pi `pinctrl` tool.
 */
class PinFunctionSnapshot {
public:
    /* Returns false, leaving the snapshot empty, if the current state cannot be read. */
    bool capture(unsigned int gpio, std::string& error);
    bool valid() const;
    /* Re-applies the captured state. No-op for an empty snapshot. */
    void restore();

private:
    std::string restore_command_;
};

} // namespace platform::gpio
