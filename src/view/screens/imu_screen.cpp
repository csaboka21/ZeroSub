/*
 * SPDX-License-Identifier: MIT
 */

#include "imu_screen.h"

#include "asset_manager.h"
#include "page_keys.h"
#include "theme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace screen {
namespace {

constexpr int32_t kSideMargin = 8;
constexpr int32_t kTextWidth = view::kScreenWidth - 2 * kSideMargin;
constexpr int32_t kStatusY = 76;
constexpr int32_t kMessageY = 94;

/* Motion grid: caption column, then X / Y / Z value columns. */
constexpr int32_t kHeaderY = 2;
constexpr int32_t kRowY = 17;
constexpr int32_t kRowStep = 14;
constexpr int32_t kTiltY = kRowY + 3 * kRowStep + 2;
constexpr std::array<int32_t, 3> kColumnX = {96, 168, 240};
constexpr int32_t kColumnWidth = 66;

/* The dial of the Compass and Level pages. */
constexpr int32_t kDialSize = 70;
constexpr int32_t kDialX = 10;
constexpr int32_t kDialY = 3;
constexpr float kDialCenterX = kDialX + kDialSize / 2.0F;
constexpr float kDialCenterY = kDialY + kDialSize / 2.0F;
constexpr float kNeedleLength = 22.0F;
constexpr float kNorthRadius = 28.0F;
constexpr int32_t kBubbleSize = 12;
/* The bubble reaches the dial's edge at this tilt. */
constexpr float kLevelRangeDeg = 20.0F;
constexpr float kLevelToleranceDeg = 1.0F;
constexpr int32_t kReadoutX = 96;
constexpr int32_t kReadoutWidth = view::kScreenWidth - kReadoutX - kSideMargin;

constexpr float kDegToRad = 0.0174532925F;

constexpr const char* kRowCaptions[3] = {"Accel  g", "Gyro  \xC2\xB0/s", "Field  uT"};
constexpr const char* kAxisNames[3] = {"X", "Y", "Z"};

std::string format_value(const char* format, float value) {
    char text[24];
    std::snprintf(text, sizeof(text), format, static_cast<double>(value));
    return text;
}

const char* compass_point(float heading_deg) {
    static constexpr const char* kPoints[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    const int index = static_cast<int>(std::lround(heading_deg / 45.0F)) % 8;
    return kPoints[index];
}

/* "BMI270 i2c-1 0x68   BMM150 aux 0x10", or what the worker is doing. */
std::string describe_status(const platform::ImuWorker::Status& status) {
    if (status.phase != platform::ImuWorker::Phase::Running || !status.text.empty()) {
        return status.text;
    }
    return "BMI270 " + status.bmi270 + "   BMM150 " + status.bmm150;
}

void set_hidden(lv_obj_t* object, bool hidden) {
    if (!object) {
        return;
    }
    if (hidden) {
        lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
    }
}

} // namespace

ImuScreen::ImuScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, model::AppPage page)
    : BaseScreen(view_model, assets), page_(page) {
    init();
}

std::vector<view::widgets::NavHint> ImuScreen::nav_hints() const {
    switch (page_) {
        case model::AppPage::ImuMotion:
            return {{"ESC", "Back"}, {"ENTER", "Zero gyro"}, {"0", "Clear"}};
        case model::AppPage::ImuCompass:
            return {{"ESC", "Back"}, {"ENTER", "Recalibrate"}};
        default:
            return {{"ESC", "Back"}, {"ENTER", "Set level"}, {"0", "Clear"}};
    }
}

bool ImuScreen::on_key(uint32_t key) {
    if (is_ok_key(key)) {
        view_model().imu_activate();
        return true;
    }
    if (key == '0') {
        view_model().imu_clear();
        return true;
    }
    if (is_left_key(key)) {
        view_model().go_back();
        return true;
    }
    return false;
}

lv_obj_t* ImuScreen::make_label(lv_obj_t* parent, const lv_font_t* font, int32_t x, int32_t y, int32_t width,
                                lv_text_align_t align) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, "");
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_obj_set_pos(label, x, y);
    return label;
}

void ImuScreen::build_content(lv_obj_t* content) {
    bold_font_ = assets().load_standard_font(12);
    if (!bold_font_) {
        bold_font_ = &lv_font_montserrat_12;
    }
    small_font_ = assets().load_standard_font(11, app::StandardFontWeight::Regular);
    if (!small_font_) {
        small_font_ = &lv_font_montserrat_12;
    }
    large_font_ = assets().load_standard_font(20);
    if (!large_font_) {
        large_font_ = &lv_font_montserrat_20;
    }

    if (page_ == model::AppPage::ImuMotion) {
        build_motion(content);
    }
    else {
        build_dial(content);
    }

    status_label_ = make_label(content, bold_font_, kSideMargin, kStatusY, kTextWidth, LV_TEXT_ALIGN_LEFT);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_DOT);
    message_label_ = make_label(content, small_font_, kSideMargin, kMessageY, kTextWidth, LV_TEXT_ALIGN_LEFT);
    lv_label_set_long_mode(message_label_, LV_LABEL_LONG_DOT);

    /* All observers are bound to the message label and fire once on registration. */
    for (auto* subject : {view_model().imu_revision_subject(),
                          view_model().imu_message_subject(),
                          view_model().dark_mode_subject()}) {
        lv_subject_add_observer_obj(subject, refresh_observer_cb, message_label_, this);
    }
}

void ImuScreen::build_motion(lv_obj_t* content) {
    for (std::size_t column = 0; column < 3; ++column) {
        headers_[column] =
            make_label(content, bold_font_, kColumnX[column], kHeaderY, kColumnWidth, LV_TEXT_ALIGN_RIGHT);
        lv_label_set_text(headers_[column], kAxisNames[column]);
    }
    for (std::size_t row = 0; row < 3; ++row) {
        const int32_t y = kRowY + static_cast<int32_t>(row) * kRowStep;
        captions_[row] = make_label(content, small_font_, kSideMargin, y, kColumnX[0] - kSideMargin, LV_TEXT_ALIGN_LEFT);
        lv_label_set_text(captions_[row], kRowCaptions[row]);
        for (std::size_t column = 0; column < 3; ++column) {
            values_[row][column] =
                make_label(content, small_font_, kColumnX[column], y, kColumnWidth, LV_TEXT_ALIGN_RIGHT);
        }
    }
    tilt_label_ = make_label(content, small_font_, kSideMargin, kTiltY, kTextWidth, LV_TEXT_ALIGN_LEFT);
}

void ImuScreen::build_dial(lv_obj_t* content) {
    dial_ = lv_obj_create(content);
    lv_obj_remove_style_all(dial_);
    lv_obj_set_size(dial_, kDialSize, kDialSize);
    lv_obj_set_pos(dial_, kDialX, kDialY);
    lv_obj_set_style_radius(dial_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dial_, 2, 0);
    lv_obj_set_style_border_opa(dial_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(dial_, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(dial_, LV_OBJ_FLAG_SCROLLABLE);

    if (page_ == model::AppPage::ImuCompass) {
        needle_ = lv_line_create(content);
        lv_obj_set_style_line_width(needle_, 3, 0);
        lv_obj_set_style_line_rounded(needle_, true, 0);
        north_label_ = lv_label_create(content);
        lv_label_set_text(north_label_, "N");
        lv_obj_set_style_text_font(north_label_, bold_font_, 0);
    }
    else {
        const float half = kDialSize / 2.0F - 4.0F;
        cross_h_points_ = {{{kDialCenterX - half, kDialCenterY}, {kDialCenterX + half, kDialCenterY}}};
        cross_v_points_ = {{{kDialCenterX, kDialCenterY - half}, {kDialCenterX, kDialCenterY + half}}};
        cross_h_ = lv_line_create(content);
        lv_line_set_points(cross_h_, cross_h_points_.data(), 2);
        cross_v_ = lv_line_create(content);
        lv_line_set_points(cross_v_, cross_v_points_.data(), 2);
        for (auto* line : {cross_h_, cross_v_}) {
            lv_obj_set_style_line_width(line, 1, 0);
        }
        /* The tolerance ring: inside it the device is level. */
        auto* ring = lv_obj_create(content);
        lv_obj_remove_style_all(ring);
        const int32_t ring_size = kBubbleSize + 6;
        lv_obj_set_size(ring, ring_size, ring_size);
        lv_obj_set_pos(ring,
                       static_cast<int32_t>(kDialCenterX) - ring_size / 2,
                       static_cast<int32_t>(kDialCenterY) - ring_size / 2);
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(ring, 1, 0);
        lv_obj_set_style_border_opa(ring, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(ring, view::palette(view_model().is_dark_mode()).text_disabled, 0);

        bubble_ = lv_obj_create(content);
        lv_obj_remove_style_all(bubble_);
        lv_obj_set_size(bubble_, kBubbleSize, kBubbleSize);
        lv_obj_set_style_radius(bubble_, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(bubble_, LV_OPA_COVER, 0);
    }

    big_label_ = make_label(content, large_font_, kReadoutX, 4, kReadoutWidth, LV_TEXT_ALIGN_LEFT);
    detail_label_ = make_label(content, small_font_, kReadoutX, 32, kReadoutWidth, LV_TEXT_ALIGN_LEFT);
    lv_label_set_long_mode(detail_label_, LV_LABEL_LONG_WRAP);
}

void ImuScreen::refresh() {
    if (!message_label_) {
        return;
    }
    const auto colors = view::palette(view_model().is_dark_mode());
    const auto& status = view_model().imu_status();

    switch (page_) {
        case model::AppPage::ImuMotion:
            refresh_motion();
            break;
        case model::AppPage::ImuCompass:
            refresh_compass();
            break;
        default:
            refresh_level();
            break;
    }

    lv_label_set_text(status_label_, describe_status(status).c_str());
    const bool failed = status.phase == platform::ImuWorker::Phase::Failed;
    lv_obj_set_style_text_color(status_label_, failed ? colors.warning : colors.primary, 0);

    lv_label_set_text(message_label_, lv_subject_get_string(view_model().imu_message_subject()));
    lv_obj_set_style_text_color(message_label_, colors.text_disabled, 0);
}

void ImuScreen::refresh_motion() {
    const auto colors = view::palette(view_model().is_dark_mode());
    const auto& reading = view_model().imu_status().reading;

    for (auto* header : headers_) {
        lv_obj_set_style_text_color(header, colors.primary, 0);
    }
    std::array<float, 3> field{};
    const bool field_valid = view_model().imu_field(field);
    const std::array<const std::array<float, 3>*, 3> rows = {&reading.accel_g, &reading.gyro_dps, &field};
    const std::array<bool, 3> valid = {reading.motion_valid, reading.motion_valid, field_valid};
    const std::array<const char*, 3> formats = {"%+.3f", "%+.1f", "%+.1f"};
    for (std::size_t row = 0; row < 3; ++row) {
        lv_obj_set_style_text_color(captions_[row], colors.text_disabled, 0);
        for (std::size_t column = 0; column < 3; ++column) {
            auto* label = values_[row][column];
            lv_label_set_text(label, valid[row] ? format_value(formats[row], (*rows[row])[column]).c_str() : "--");
            lv_obj_set_style_text_color(label, valid[row] ? colors.text : colors.text_disabled, 0);
        }
    }

    std::string tilt;
    float pitch = 0.0F;
    float roll = 0.0F;
    if (view_model().imu_tilt(pitch, roll)) {
        tilt = "Pitch " + format_value("%+.1f\xC2\xB0", pitch) + "   Roll " + format_value("%+.1f\xC2\xB0", roll);
    }
    if (reading.temperature_valid) {
        tilt += (tilt.empty() ? "" : "   ") + std::string("Temp ") + format_value("%.1f \xC2\xB0" "C", reading.temperature_c);
    }
    lv_label_set_text(tilt_label_, tilt.c_str());
    lv_obj_set_style_text_color(tilt_label_, colors.text, 0);
}

void ImuScreen::refresh_compass() {
    const auto colors = view::palette(view_model().is_dark_mode());
    lv_obj_set_style_border_color(dial_, colors.border, 0);
    lv_obj_set_style_line_color(needle_, colors.error, 0);
    lv_obj_set_style_text_color(north_label_, colors.text, 0);
    lv_obj_set_style_text_color(big_label_, colors.text, 0);
    lv_obj_set_style_text_color(detail_label_, colors.text, 0);

    std::array<float, 3> field{};
    if (!view_model().imu_field(field)) {
        lv_label_set_text(big_label_, "--");
        lv_label_set_text(detail_label_, view_model().imu_running() ? "No BMM150 reading" : "");
        set_hidden(needle_, true);
        set_hidden(north_label_, true);
        return;
    }

    const float strength = std::sqrt(field[0] * field[0] + field[1] * field[1] + field[2] * field[2]);
    char detail[96];
    std::snprintf(detail,
                  sizeof(detail),
                  "X %+.1f  Y %+.1f  Z %+.1f uT\n|B| %.1f uT",
                  static_cast<double>(field[0]),
                  static_cast<double>(field[1]),
                  static_cast<double>(field[2]),
                  static_cast<double>(strength));
    lv_label_set_text(detail_label_, detail);

    float heading = 0.0F;
    if (!view_model().imu_heading(heading)) {
        const auto percent = view_model().imu_calibration_percent();
        lv_label_set_text(big_label_, ("Calibrating " + std::to_string(percent) + "%").c_str());
        lv_obj_set_style_text_color(big_label_, colors.warning, 0);
        set_hidden(needle_, true);
        set_hidden(north_label_, true);
        return;
    }

    char big[24];
    std::snprintf(big, sizeof(big), "%.0f\xC2\xB0 %s", static_cast<double>(heading), compass_point(heading));
    lv_label_set_text(big_label_, big);

    /* North sits at -heading from the top of the dial. */
    const float angle = -heading * kDegToRad;
    const float dx = std::sin(angle);
    const float dy = -std::cos(angle);
    needle_points_ = {{{kDialCenterX, kDialCenterY},
                       {kDialCenterX + dx * kNeedleLength, kDialCenterY + dy * kNeedleLength}}};
    lv_line_set_points(needle_, needle_points_.data(), 2);
    lv_obj_update_layout(north_label_);
    lv_obj_set_pos(north_label_,
                   static_cast<int32_t>(kDialCenterX + dx * kNorthRadius) - lv_obj_get_width(north_label_) / 2,
                   static_cast<int32_t>(kDialCenterY + dy * kNorthRadius) - lv_obj_get_height(north_label_) / 2);
    set_hidden(needle_, false);
    set_hidden(north_label_, false);
}

void ImuScreen::refresh_level() {
    const auto colors = view::palette(view_model().is_dark_mode());
    lv_obj_set_style_border_color(dial_, colors.border, 0);
    for (auto* line : {cross_h_, cross_v_}) {
        lv_obj_set_style_line_color(line, colors.border, 0);
    }
    lv_obj_set_style_text_color(detail_label_, colors.text, 0);

    float pitch = 0.0F;
    float roll = 0.0F;
    if (!view_model().imu_tilt(pitch, roll)) {
        lv_label_set_text(big_label_, "--");
        lv_obj_set_style_text_color(big_label_, colors.text_disabled, 0);
        lv_label_set_text(detail_label_, view_model().imu_running() ? "No BMI270 reading" : "");
        set_hidden(bubble_, true);
        return;
    }

    const float tilt = std::sqrt(pitch * pitch + roll * roll);
    const bool level = tilt <= kLevelToleranceDeg;
    lv_label_set_text(big_label_, level ? "Level" : format_value("%.1f\xC2\xB0", tilt).c_str());
    lv_obj_set_style_text_color(big_label_, level ? colors.success : colors.text, 0);
    lv_label_set_text(detail_label_,
                      ("Pitch " + format_value("%+.1f\xC2\xB0", pitch) + "\nRoll " +
                       format_value("%+.1f\xC2\xB0", roll))
                          .c_str());

    /* The bubble floats to the high side, so it moves against the tilt; it stops at the rim. */
    const float reach = kDialSize / 2.0F - kBubbleSize / 2.0F - 3.0F;
    float x = -roll / kLevelRangeDeg;
    float y = pitch / kLevelRangeDeg;
    const float length = std::sqrt(x * x + y * y);
    if (length > 1.0F) {
        x /= length;
        y /= length;
    }
    lv_obj_set_pos(bubble_,
                   static_cast<int32_t>(std::lround(kDialCenterX + x * reach)) - kBubbleSize / 2,
                   static_cast<int32_t>(std::lround(kDialCenterY + y * reach)) - kBubbleSize / 2);
    lv_obj_set_style_bg_color(bubble_, level ? colors.success : colors.warning, 0);
    set_hidden(bubble_, false);
}

void ImuScreen::refresh_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<ImuScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen
