/*
 * SPDX-License-Identifier: MIT
 */

#include "imu_worker.h"

#include "i2c_device.h"
#include "imu_sensors.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

namespace platform {
namespace {

constexpr std::array<uint8_t, 2> kBmi270Addresses = {0x68, 0x69};
constexpr std::array<uint8_t, 4> kBmm150Addresses = {0x10, 0x11, 0x12, 0x13};

/* Read periods while running. */
constexpr unsigned int kMotionPeriodMs = 40;
constexpr unsigned int kFieldEveryTicks = 2;
constexpr unsigned int kTemperatureEveryTicks = 25;

/* Consecutive failed reads before the worker gives up on a chip. */
constexpr unsigned int kMaxReadFailures = 25;

constexpr float kStandardGravity = 9.80665F;
constexpr float kRadToDeg = 57.2957795F;

std::string hex_address(uint8_t address) {
    char text[8];
    std::snprintf(text, sizeof(text), "0x%02X", address);
    return text;
}

std::string bus_name(const std::string& path) {
    return std::filesystem::path(path).filename().string();
}

/* ---- IIO sysfs ---------------------------------------------------------------- */

bool read_number(const std::filesystem::path& path, float& value) {
    std::ifstream input(path);
    double parsed = 0.0;
    if (!(input >> parsed)) {
        return false;
    }
    value = static_cast<float>(parsed);
    return true;
}

/* `<prefix>_<axis>_scale`, else the shared `<prefix>_scale`. */
bool read_scale(const std::filesystem::path& device, const std::string& prefix, const char* axis, float& scale) {
    return read_number(device / (prefix + "_" + axis + "_scale"), scale) ||
           read_number(device / (prefix + "_scale"), scale);
}

bool read_vector(const std::filesystem::path& device, const std::string& prefix, float unit, std::array<float, 3>& out) {
    static constexpr const char* kAxes[3] = {"x", "y", "z"};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        float raw = 0.0F;
        float scale = 1.0F;
        if (!read_number(device / (prefix + "_" + kAxes[axis] + "_raw"), raw) ||
            !read_scale(device, prefix, kAxes[axis], scale)) {
            return false;
        }
        out[axis] = raw * scale * unit;
    }
    return true;
}

bool read_iio_temperature(const std::filesystem::path& device, float& celsius) {
    float raw = 0.0F;
    float offset = 0.0F;
    float scale = 1.0F;
    if (!read_number(device / "in_temp_raw", raw)) {
        return false;
    }
    read_number(device / "in_temp_offset", offset);
    read_number(device / "in_temp_scale", scale);
    /* IIO temperatures are in millidegrees. */
    celsius = (raw + offset) * scale / 1000.0F;
    return true;
}

/* The first IIO device whose name contains one of `names`. */
std::filesystem::path find_iio_device(std::initializer_list<const char*> names) {
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator("/sys/bus/iio/devices", error)) {
        std::ifstream input(entry.path() / "name");
        std::string name;
        if (!std::getline(input, name)) {
            continue;
        }
        for (const char* wanted : names) {
            if (name.find(wanted) != std::string::npos) {
                return entry.path();
            }
        }
    }
    return {};
}

} // namespace

std::unique_ptr<ImuWorker> ImuWorker::start(const ImuConfig& config) {
    std::unique_ptr<ImuWorker> worker(new ImuWorker(config));
    worker->thread_ = std::thread(&ImuWorker::run, worker.get());
    return worker;
}

ImuWorker::ImuWorker(const ImuConfig& config) : config_(config) {
    status_.text = "Looking for the IMU...";
}

ImuWorker::~ImuWorker() {
    cancel_ = true;
    if (thread_.joinable()) {
        thread_.join();
    }
}

uint32_t ImuWorker::revision() const {
    return revision_.load();
}

ImuWorker::Status ImuWorker::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

void ImuWorker::publish(const Status& status) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = status;
    }
    ++revision_;
}

bool ImuWorker::cancelled() const {
    return cancel_.load();
}

bool ImuWorker::sleep_for(unsigned int duration_ms) const {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(duration_ms);
    while (!cancelled() && std::chrono::steady_clock::now() < until) {
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min(duration_ms, 10U)));
    }
    return !cancelled();
}

void ImuWorker::run() {
    std::string i2c_why;
    if (run_i2c(i2c_why) || cancelled()) {
        return;
    }
    std::string iio_why;
    if (run_iio(iio_why) || cancelled()) {
        return;
    }
    Status failed;
    failed.phase = Phase::Failed;
    failed.bmi270 = "not found";
    failed.bmm150 = "not found";
    failed.text = i2c_why;
    publish(failed);
}

bool ImuWorker::run_i2c(std::string& why) {
    Status status;
    status.text = "Looking for the BMI270...";
    publish(status);

    const std::vector<std::string> buses =
        config_.i2c_device.empty() ? i2c::list_buses() : std::vector<std::string>{config_.i2c_device};
    if (buses.empty()) {
        why = "No /dev/i2c-* bus (enable I2C in config.txt)";
        return false;
    }

    /* The BMI270. */
    std::unique_ptr<Bmi270> bmi;
    bool kernel_owned = false;
    std::string open_error;
    for (const auto& bus : buses) {
        for (const uint8_t address : kBmi270Addresses) {
            bool busy = false;
            auto device = i2c::Device::open(bus, address, open_error, &busy);
            kernel_owned = kernel_owned || busy;
            if (device && Bmi270::probe(*device)) {
                bmi = std::make_unique<Bmi270>(std::move(device));
                break;
            }
        }
        if (bmi || cancelled()) {
            break;
        }
    }
    if (cancelled()) {
        return true;
    }

    std::string bmi_where;
    std::string bus_used;
    if (bmi) {
        bus_used = bmi->device().bus();
        bmi_where = bus_name(bus_used) + " " + hex_address(bmi->device().address());
        status.text = "Loading the BMI270 config...";
        publish(status);
        std::string error;
        if (!bmi->init(error)) {
            why = error;
            return false;
        }
    }
    else if (kernel_owned) {
        /* A kernel driver owns it: leave it to the IIO path. */
        why = "BMI270 is claimed by a kernel driver";
        return false;
    }

    /* The BMM150: behind the BMI270 first, then on the bus itself. */
    std::unique_ptr<Bmm150> bmm;
    std::string bmm_where;
    if (bmi) {
        for (const uint8_t address : kBmm150Addresses) {
            if (!bmi->enable_aux(address)) {
                break;
            }
            auto candidate = std::make_unique<Bmm150>(std::make_unique<AuxMagnetometerBus>(*bmi));
            if (candidate->probe()) {
                bmm = std::move(candidate);
                bmm_where = "aux " + hex_address(address);
                break;
            }
        }
    }
    /* Only the BMI270's bus is searched, so other chips at 0x10-0x13 elsewhere are not written to. */
    const std::vector<std::string> mag_buses = bus_used.empty() ? buses : std::vector<std::string>{bus_used};
    for (const auto& bus : mag_buses) {
        if (bmm || cancelled()) {
            break;
        }
        for (const uint8_t address : kBmm150Addresses) {
            auto device = i2c::Device::open(bus, address, open_error);
            if (!device) {
                continue;
            }
            auto candidate = std::make_unique<Bmm150>(std::make_unique<DirectMagnetometerBus>(std::move(device)));
            if (candidate->probe()) {
                bmm = std::move(candidate);
                bmm_where = bus_name(bus) + " " + hex_address(address);
                break;
            }
        }
    }
    if (cancelled()) {
        return true;
    }
    if (bmm) {
        std::string error;
        if (!bmm->init(error)) {
            bmm_where = error;
            bmm.reset();
        }
        else {
        }
    }

    if (!bmi && !bmm) {
        why = "No BMI270 or BMM150 on " + (config_.i2c_device.empty() ? std::string("any I2C bus") : config_.i2c_device);
        return false;
    }

    status.phase = Phase::Running;
    status.bmi270 = bmi ? bmi_where : "not found";
    status.bmm150 = bmm ? bmm_where : (bmm_where.empty() ? "not found" : bmm_where);
    status.text.clear();
    publish(status);

    unsigned int tick = 0;
    unsigned int motion_failures = 0;
    unsigned int field_failures = 0;
    while (sleep_for(kMotionPeriodMs)) {
        bool changed = false;
        if (bmi) {
            if (bmi->read_motion(status.reading.accel_g, status.reading.gyro_dps)) {
                status.reading.motion_valid = true;
                motion_failures = 0;
                changed = true;
            }
            else if (++motion_failures >= kMaxReadFailures && status.reading.motion_valid) {
                status.reading.motion_valid = false;
                status.text = "BMI270 stopped answering";
                changed = true;
            }
            if (tick % kTemperatureEveryTicks == 0) {
                status.reading.temperature_valid = bmi->read_temperature(status.reading.temperature_c);
                changed = true;
            }
        }
        if (bmm && tick % kFieldEveryTicks == 0) {
            if (bmm->read_field(status.reading.field_ut)) {
                status.reading.field_valid = true;
                field_failures = 0;
                changed = true;
            }
            else if (++field_failures >= kMaxReadFailures / kFieldEveryTicks && status.reading.field_valid) {
                status.reading.field_valid = false;
                changed = true;
            }
        }
        ++tick;
        if (changed) {
            ++status.samples;
            publish(status);
        }
    }
    return true;
}

bool ImuWorker::run_iio(std::string& why) {
    const auto motion = find_iio_device({"bmi270"});
    const auto field = find_iio_device({"bmm150", "bmc150_magn"});
    if (motion.empty() && field.empty()) {
        why = "no IIO device either";
        return false;
    }

    Status status;
    status.phase = Phase::Running;
    status.bmi270 = motion.empty() ? "not found" : "kernel " + motion.filename().string();
    status.bmm150 = field.empty() ? "not found" : "kernel " + field.filename().string();
    publish(status);

    unsigned int tick = 0;
    while (sleep_for(kMotionPeriodMs)) {
        auto& reading = status.reading;
        if (!motion.empty()) {
            /* IIO reports m/s^2 and rad/s. */
            reading.motion_valid = read_vector(motion, "in_accel", 1.0F / kStandardGravity, reading.accel_g) &&
                                   read_vector(motion, "in_anglvel", kRadToDeg, reading.gyro_dps);
            if (tick % kTemperatureEveryTicks == 0) {
                reading.temperature_valid = read_iio_temperature(motion, reading.temperature_c);
            }
        }
        if (!field.empty() && tick % kFieldEveryTicks == 0) {
            /* IIO reports gauss; 1 G = 100 uT. */
            reading.field_valid = read_vector(field, "in_magn", 100.0F, reading.field_ut);
        }
        ++tick;
        ++status.samples;
        publish(status);
    }
    return true;
}

} // namespace platform
