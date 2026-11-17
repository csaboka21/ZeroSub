/*
 * SPDX-License-Identifier: MIT
 */

#include "base_viewmodel.h"

#include <algorithm>
#include <cmath>

namespace viewmodel {
namespace {

constexpr uint32_t kImuPollPeriodMs = 50;
constexpr float kRadToDeg = 57.2957795F;
/* A compass axis counts as calibrated once it has seen this much of the earth's field (25-65 uT). */
constexpr float kCalibrationSpanUt = 40.0F;

bool is_imu_page(model::AppPage page) {
    return page == model::AppPage::ImuMenu || page == model::AppPage::ImuMotion ||
           page == model::AppPage::ImuCompass || page == model::AppPage::ImuLevel;
}

float wrap_degrees(float degrees) {
    while (degrees > 180.0F) {
        degrees -= 360.0F;
    }
    while (degrees < -180.0F) {
        degrees += 360.0F;
    }
    return degrees;
}

} // namespace

lv_subject_t* BaseViewModel::imu_revision_subject() {
    return imu_revision_subject_.native();
}

lv_subject_t* BaseViewModel::imu_message_subject() {
    return imu_message_subject_.native();
}

const platform::ImuWorker::Status& BaseViewModel::imu_status() const {
    return imu_status_;
}

bool BaseViewModel::imu_running() const {
    return imu_status_.phase == platform::ImuWorker::Phase::Running;
}

void BaseViewModel::configure_imu(const platform::ImuConfig& config) {
    imu_config_ = config;
}

void BaseViewModel::imu_touch() {
    imu_revision_subject_.set(imu_revision_subject_.value() + 1);
}

bool BaseViewModel::imu_tilt(float& pitch_deg, float& roll_deg) const {
    const auto& reading = imu_status_.reading;
    if (!reading.motion_valid) {
        return false;
    }
    const auto& a = reading.accel_g;
    const float magnitude = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (magnitude < 0.2F) {
        /* Free fall: no direction to speak of. */
        return false;
    }
    pitch_deg = wrap_degrees(std::atan2(-a[0], std::sqrt(a[1] * a[1] + a[2] * a[2])) * kRadToDeg - imu_pitch_zero_);
    roll_deg = wrap_degrees(std::atan2(a[1], a[2]) * kRadToDeg - imu_roll_zero_);
    return true;
}

bool BaseViewModel::imu_field(std::array<float, 3>& field_ut) const {
    const auto& reading = imu_status_.reading;
    if (!reading.field_valid) {
        return false;
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const bool spanned = imu_field_seen_ && imu_field_max_[axis] - imu_field_min_[axis] >= kCalibrationSpanUt;
        const float offset = spanned ? (imu_field_max_[axis] + imu_field_min_[axis]) / 2.0F : 0.0F;
        field_ut[axis] = reading.field_ut[axis] - offset;
    }
    return true;
}

int32_t BaseViewModel::imu_calibration_percent() const {
    if (!imu_field_seen_) {
        return 0;
    }
    /* The heading uses X and Y only. */
    float progress = 0.0F;
    for (std::size_t axis = 0; axis < 2; ++axis) {
        progress += std::min(1.0F, (imu_field_max_[axis] - imu_field_min_[axis]) / kCalibrationSpanUt);
    }
    return static_cast<int32_t>(progress * 50.0F);
}

bool BaseViewModel::imu_heading(float& heading_deg) const {
    std::array<float, 3> field{};
    if (imu_calibration_percent() < 100 || !imu_field(field)) {
        return false;
    }
    heading_deg = std::atan2(field[1], field[0]) * kRadToDeg;
    if (heading_deg < 0.0F) {
        heading_deg += 360.0F;
    }
    return true;
}

void BaseViewModel::imu_activate() {
    switch (model_.current_page()) {
        case model::AppPage::ImuMotion:
            if (imu_status_.reading.motion_valid) {
                /* The reading shown has the old offset removed; add it back to get the raw rate. */
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    imu_gyro_offset_[axis] += imu_status_.reading.gyro_dps[axis];
                    imu_status_.reading.gyro_dps[axis] = 0.0F;
                }
                imu_message_subject_.set("Gyro zeroed: keep the device still while doing this");
            }
            break;
        case model::AppPage::ImuCompass:
            imu_field_seen_ = false;
            imu_message_subject_.set("Calibration restarted");
            break;
        case model::AppPage::ImuLevel: {
            float pitch = 0.0F;
            float roll = 0.0F;
            if (imu_tilt(pitch, roll)) {
                imu_pitch_zero_ += pitch;
                imu_roll_zero_ += roll;
                imu_message_subject_.set("This tilt is now level");
            }
            break;
        }
        default:
            return;
    }
    imu_touch();
}

void BaseViewModel::imu_clear() {
    switch (model_.current_page()) {
        case model::AppPage::ImuMotion:
            imu_gyro_offset_.fill(0.0F);
            imu_message_subject_.set("Gyro offset cleared");
            break;
        case model::AppPage::ImuLevel:
            imu_pitch_zero_ = 0.0F;
            imu_roll_zero_ = 0.0F;
            imu_message_subject_.set("Level zero cleared");
            break;
        default:
            return;
    }
    imu_touch();
}

void BaseViewModel::imu_set_message() {
    switch (model_.current_page()) {
        case model::AppPage::ImuMotion:
            imu_message_subject_.set("ENTER zeroes the gyro (hold still)");
            break;
        case model::AppPage::ImuCompass:
            imu_message_subject_.set("Turn the device around every axis to calibrate");
            break;
        case model::AppPage::ImuLevel:
            imu_message_subject_.set("ENTER takes this tilt as level");
            break;
        default:
            imu_message_subject_.set("");
            break;
    }
}

void BaseViewModel::sync_imu_activity() {
    if (!is_imu_page(model_.current_page())) {
        /* Leaving the tool stops the thread; the next visit sets the chips up again. */
        imu_worker_.reset();
        imu_status_ = platform::ImuWorker::Status{};
        if (imu_timer_) {
            lv_timer_pause(imu_timer_);
        }
        return;
    }

    imu_set_message();
    if (!imu_config_.enabled) {
        imu_status_ = platform::ImuWorker::Status{};
        imu_status_.phase = platform::ImuWorker::Phase::Failed;
        imu_status_.text = "Set [imu] enabled=yes in zerosub.conf";
        imu_touch();
        return;
    }
    if (!imu_worker_) {
        imu_worker_ = platform::ImuWorker::start(imu_config_);
        imu_seen_revision_ = imu_worker_->revision() - 1;
    }
    if (!imu_timer_) {
        imu_timer_ = lv_timer_create(imu_timer_cb, kImuPollPeriodMs, this);
    }
    else {
        lv_timer_resume(imu_timer_);
    }
    poll_imu();
    imu_touch();
}

void BaseViewModel::poll_imu() {
    if (!imu_worker_) {
        return;
    }
    const uint32_t revision = imu_worker_->revision();
    if (revision == imu_seen_revision_) {
        return;
    }
    imu_seen_revision_ = revision;
    imu_status_ = imu_worker_->status();

    auto& reading = imu_status_.reading;
    if (reading.motion_valid) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            reading.gyro_dps[axis] -= imu_gyro_offset_[axis];
        }
    }
    /* Calibration only learns while the compass is on screen, so the user knows to turn the device. */
    if (reading.field_valid && model_.current_page() == model::AppPage::ImuCompass) {
        if (!imu_field_seen_) {
            imu_field_min_ = reading.field_ut;
            imu_field_max_ = reading.field_ut;
            imu_field_seen_ = true;
        }
        for (std::size_t axis = 0; axis < 3; ++axis) {
            imu_field_min_[axis] = std::min(imu_field_min_[axis], reading.field_ut[axis]);
            imu_field_max_[axis] = std::max(imu_field_max_[axis], reading.field_ut[axis]);
        }
    }
    imu_touch();
}

void BaseViewModel::imu_timer_cb(lv_timer_t* timer) {
    auto* view_model = static_cast<BaseViewModel*>(lv_timer_get_user_data(timer));
    if (view_model) {
        view_model->poll_imu();
    }
}

} // namespace viewmodel
