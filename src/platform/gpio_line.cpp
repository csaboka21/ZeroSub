/*
 * SPDX-License-Identifier: MIT
 */

#include "gpio_line.h"

#if !USE_DESKTOP && defined(__linux__)
#define ZEROSUB_HAVE_GPIO 1
#else
#define ZEROSUB_HAVE_GPIO 0
#endif

#if ZEROSUB_HAVE_GPIO
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <linux/gpio.h>
#include <poll.h>
#include <sstream>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <vector>
#endif

namespace platform::gpio {

OutputLine::OutputLine(int fd) : fd_(fd) {}

EdgeInput::EdgeInput(int fd) : fd_(fd) {}

InputLine::InputLine(int fd) : fd_(fd) {}

#if ZEROSUB_HAVE_GPIO

namespace {

std::string errno_text(const std::string& what) {
    return what + ": " + std::strerror(errno);
}

bool is_pin_function(const std::string& token) {
    if (token == "ip" || token == "op" || token == "no") {
        return true;
    }
    return token.size() == 2 && token[0] == 'a' && token[1] >= '0' && token[1] <= '8';
}

bool is_pull(const std::string& token) {
    return token == "pn" || token == "pu" || token == "pd" || token == "--";
}

} // namespace

std::unique_ptr<OutputLine> OutputLine::request(const std::string& chip_path,
                                                unsigned int line_offset,
                                                bool initial_value,
                                                std::string& error) {
    error.clear();
    const int chip_fd = ::open(chip_path.c_str(), O_RDWR | O_CLOEXEC);
    if (chip_fd < 0) {
        error = errno_text("open " + chip_path);
        return nullptr;
    }

    gpio_v2_line_request request;
    std::memset(&request, 0, sizeof(request));
    request.offsets[0] = line_offset;
    request.num_lines = 1;
    std::snprintf(request.consumer, sizeof(request.consumer), "zerosub");
    request.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
    request.config.num_attrs = 1;
    request.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
    request.config.attrs[0].attr.values = initial_value ? 1 : 0;
    request.config.attrs[0].mask = 1;

    const int result = ::ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &request);
    if (result < 0) {
        error = errno_text("request GPIO" + std::to_string(line_offset));
    }
    ::close(chip_fd);
    if (result < 0) {
        return nullptr;
    }

    return std::unique_ptr<OutputLine>(new OutputLine(request.fd));
}

OutputLine::~OutputLine() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

bool OutputLine::set_value(bool active, std::string& error) {
    error.clear();
    gpio_v2_line_values values;
    std::memset(&values, 0, sizeof(values));
    values.mask = 1;
    values.bits = active ? 1 : 0;
    if (::ioctl(fd_, GPIO_V2_LINE_SET_VALUES_IOCTL, &values) < 0) {
        error = errno_text("set GPIO value");
        return false;
    }
    return true;
}

std::unique_ptr<InputLine> InputLine::request(const std::string& chip_path,
                                              unsigned int line_offset,
                                              std::string& error) {
    error.clear();
    const int chip_fd = ::open(chip_path.c_str(), O_RDWR | O_CLOEXEC);
    if (chip_fd < 0) {
        error = errno_text("open " + chip_path);
        return nullptr;
    }

    gpio_v2_line_request request;
    std::memset(&request, 0, sizeof(request));
    request.offsets[0] = line_offset;
    request.num_lines = 1;
    std::snprintf(request.consumer, sizeof(request.consumer), "zerosub");
    request.config.flags = GPIO_V2_LINE_FLAG_INPUT;

    const int result = ::ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &request);
    if (result < 0) {
        error = errno_text("request GPIO" + std::to_string(line_offset));
    }
    ::close(chip_fd);
    if (result < 0) {
        return nullptr;
    }

    return std::unique_ptr<InputLine>(new InputLine(request.fd));
}

InputLine::~InputLine() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

bool InputLine::active() const {
    gpio_v2_line_values values;
    std::memset(&values, 0, sizeof(values));
    values.mask = 1;
    if (::ioctl(fd_, GPIO_V2_LINE_GET_VALUES_IOCTL, &values) < 0) {
        return false;
    }
    return (values.bits & 1) != 0;
}

std::unique_ptr<EdgeInput> EdgeInput::request(const std::string& chip_path,
                                              unsigned int line_offset,
                                              std::string& error) {
    error.clear();
    const int chip_fd = ::open(chip_path.c_str(), O_RDWR | O_CLOEXEC);
    if (chip_fd < 0) {
        error = errno_text("open " + chip_path);
        return nullptr;
    }

    gpio_v2_line_request request;
    std::memset(&request, 0, sizeof(request));
    request.offsets[0] = line_offset;
    request.num_lines = 1;
    std::snprintf(request.consumer, sizeof(request.consumer), "zerosub-irq");
    request.config.flags = GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_EDGE_RISING;

    const int result = ::ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &request);
    if (result < 0) {
        error = errno_text("request GPIO" + std::to_string(line_offset));
    }
    ::close(chip_fd);
    if (result < 0) {
        return nullptr;
    }

    return std::unique_ptr<EdgeInput>(new EdgeInput(request.fd));
}

EdgeInput::~EdgeInput() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

bool EdgeInput::active() const {
    gpio_v2_line_values values;
    std::memset(&values, 0, sizeof(values));
    values.mask = 1;
    if (::ioctl(fd_, GPIO_V2_LINE_GET_VALUES_IOCTL, &values) < 0) {
        return false;
    }
    return (values.bits & 1) != 0;
}

bool EdgeInput::wait(unsigned int timeout_us) {
    pollfd descriptor;
    descriptor.fd = fd_;
    descriptor.events = POLLIN;
    descriptor.revents = 0;

    timespec timeout;
    timeout.tv_sec = static_cast<time_t>(timeout_us / 1000000U);
    timeout.tv_nsec = static_cast<long>(timeout_us % 1000000U) * 1000L;
    if (::ppoll(&descriptor, 1, &timeout, nullptr) <= 0 || (descriptor.revents & POLLIN) == 0) {
        return false;
    }

    /* Drain the queue: the caller only needs to know that the line went high. */
    gpio_v2_line_event events[16];
    bool seen = false;
    for (;;) {
        const ssize_t size = ::read(fd_, events, sizeof(events));
        if (size <= 0) {
            break;
        }
        seen = true;
        descriptor.revents = 0;
        if (::poll(&descriptor, 1, 0) <= 0 || (descriptor.revents & POLLIN) == 0) {
            break;
        }
    }
    return seen;
}

bool PinFunctionSnapshot::capture(unsigned int gpio, std::string& error) {
    error.clear();
    restore_command_.clear();

    const std::string number = std::to_string(gpio);
    const std::string command = "pinctrl get " + number + " 2>/dev/null";
    FILE* pipe = ::popen(command.c_str(), "r");
    if (!pipe) {
        error = "could not run pinctrl";
        return false;
    }
    char buffer[256] = {0};
    const bool have_line = std::fgets(buffer, sizeof(buffer), pipe) != nullptr;
    const int status = ::pclose(pipe);
    if (!have_line || status != 0) {
        error = "pinctrl get " + number + " failed";
        return false;
    }

    /* Expected shape: "14: a5    pn | hi // ..." or "26: op dh pn | hi // ...". */
    std::istringstream stream(buffer);
    std::vector<std::string> tokens;
    for (std::string token; stream >> token;) {
        tokens.push_back(token);
    }

    std::size_t index = 0;
    if (tokens.size() < 3 || tokens[index++] != number + ":" || !is_pin_function(tokens[index])) {
        error = "unexpected pinctrl output for GPIO" + number;
        return false;
    }
    const std::string function = tokens[index++];

    std::string level;
    if (function == "op") {
        level = tokens[index++];
        if (level != "dh" && level != "dl" && level != "--") {
            error = "unexpected pinctrl output for GPIO" + number;
            return false;
        }
    }

    if (index >= tokens.size() || !is_pull(tokens[index])) {
        error = "unexpected pinctrl output for GPIO" + number;
        return false;
    }
    const std::string pull = tokens[index++];

    if (level == "--") {
        /* No explicit drive level reported: fall back to the sampled level after the "|". */
        const bool sampled_high = index + 1 < tokens.size() && tokens[index] == "|" &&
                                  tokens[index + 1] == "hi";
        level = sampled_high ? "dh" : "dl";
    }

    restore_command_ = "pinctrl set " + number + " " + function;
    if (!level.empty()) {
        restore_command_ += " " + level;
    }
    if (pull != "--") {
        restore_command_ += " " + pull;
    }
    return true;
}

void PinFunctionSnapshot::restore() {
    if (restore_command_.empty()) {
        return;
    }
    const int ignored = std::system(restore_command_.c_str());
    (void)ignored;
    restore_command_.clear();
}

#else // !ZEROSUB_HAVE_GPIO

std::unique_ptr<OutputLine> OutputLine::request(const std::string&,
                                                unsigned int,
                                                bool,
                                                std::string& error) {
    error = "GPIO is only available on the device";
    return nullptr;
}

OutputLine::~OutputLine() = default;

bool OutputLine::set_value(bool, std::string& error) {
    error = "GPIO is only available on the device";
    return false;
}

std::unique_ptr<InputLine> InputLine::request(const std::string&, unsigned int, std::string& error) {
    error = "GPIO is only available on the device";
    return nullptr;
}

InputLine::~InputLine() = default;

bool InputLine::active() const {
    return false;
}

std::unique_ptr<EdgeInput> EdgeInput::request(const std::string&, unsigned int, std::string& error) {
    error = "GPIO is only available on the device";
    return nullptr;
}

EdgeInput::~EdgeInput() = default;

bool EdgeInput::active() const {
    return false;
}

bool EdgeInput::wait(unsigned int) {
    return false;
}

bool PinFunctionSnapshot::capture(unsigned int, std::string& error) {
    error = "GPIO is only available on the device";
    return false;
}

void PinFunctionSnapshot::restore() {}

#endif

bool PinFunctionSnapshot::valid() const {
    return !restore_command_.empty();
}

} // namespace platform::gpio
