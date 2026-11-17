/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "screen_manager.h"

#include "add_manually_screen.h"
#include "frequency_analyzer_screen.h"
#include "imu_screen.h"
#include "info_screen.h"
#include "infrared_screen.h"
#include "menu_screen.h"
#include "nfc_screen.h"
#include "rfid_screen.h"
#include "read_raw_screen.h"
#include "read_screen.h"
#include "saved_screen.h"
#include "signal_meter_screen.h"
#include "web_screen.h"

namespace app {
namespace {

void flush_requested_page_async(void* user_data) {
    auto* manager = static_cast<ScreenManager*>(user_data);
    if (manager) {
        manager->flush_requested_page();
    }
}

void current_page_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* manager = static_cast<ScreenManager*>(lv_observer_get_target(observer));
    if (!manager) {
        return;
    }

    manager->request_page(static_cast<model::AppPage>(lv_subject_get_int(subject)));
}

} // namespace

ScreenManager::ScreenManager(viewmodel::BaseViewModel& view_model, AssetManager& assets)
    : view_model_(view_model), assets_(assets) {}

ScreenManager::~ScreenManager() {
    lv_async_call_cancel(flush_requested_page_async, this);

    if (page_observer_) {
        lv_observer_remove(page_observer_);
        page_observer_ = nullptr;
    }
}

void ScreenManager::start() {
    if (page_observer_) {
        return;
    }

    page_observer_ = lv_subject_add_observer_with_target(view_model_.current_page_subject(),
                                                         current_page_observer,
                                                         this,
                                                         nullptr);
}

void ScreenManager::show_page(model::AppPage page) {
    std::unique_ptr<screen::BaseScreen> next_screen;
    switch (page) {
        case model::AppPage::FrequencyAnalyzer:
            next_screen = std::make_unique<screen::FrequencyAnalyzerScreen>(view_model_, assets_);
            break;
        case model::AppPage::SignalMeter:
            next_screen = std::make_unique<screen::SignalMeterScreen>(view_model_, assets_);
            break;
        case model::AppPage::ReadRaw:
            next_screen = std::make_unique<screen::ReadRawScreen>(view_model_, assets_);
            break;
        case model::AppPage::Read:
            next_screen = std::make_unique<screen::ReadScreen>(view_model_, assets_);
            break;
        case model::AppPage::Saved:
            next_screen = std::make_unique<screen::SavedScreen>(view_model_, assets_);
            break;
        case model::AppPage::AddManually:
            next_screen = std::make_unique<screen::AddManuallyScreen>(view_model_, assets_);
            break;
        case model::AppPage::Info:
            next_screen = std::make_unique<screen::InfoScreen>(view_model_, assets_);
            break;
        case model::AppPage::IrLearn:
        case model::AppPage::IrSaved:
        case model::AppPage::IrRemote:
            next_screen = std::make_unique<screen::InfraredScreen>(view_model_, assets_, page);
            break;
        case model::AppPage::NfcRead:
        case model::AppPage::NfcSaved:
        case model::AppPage::NfcCard:
            next_screen = std::make_unique<screen::NfcScreen>(view_model_, assets_, page);
            break;
        case model::AppPage::RfidSaved:
        case model::AppPage::RfidKey:
        case model::AppPage::RfidAdd:
        case model::AppPage::RfidRead:
            next_screen = std::make_unique<screen::RfidScreen>(view_model_, assets_, page);
            break;
        case model::AppPage::ImuMotion:
        case model::AppPage::ImuCompass:
        case model::AppPage::ImuLevel:
            next_screen = std::make_unique<screen::ImuScreen>(view_model_, assets_, page);
            break;
        case model::AppPage::WebFiles:
            next_screen = std::make_unique<screen::WebScreen>(view_model_, assets_);
            break;
        case model::AppPage::Menu:
        case model::AppPage::SubGhzMenu:
        case model::AppPage::InfraredMenu:
        case model::AppPage::NfcMenu:
        case model::AppPage::RfidMenu:
        case model::AppPage::ImuMenu:
            next_screen = std::make_unique<screen::MenuScreen>(view_model_, assets_, page);
            break;
    }

    load_screen(std::move(next_screen));
    loaded_page_ = page;
    has_loaded_page_ = true;
}

void ScreenManager::request_page(model::AppPage page) {
    requested_page_ = page;

    if (page_switch_scheduled_) {
        return;
    }

    page_switch_scheduled_ = lv_async_call(flush_requested_page_async, this) == LV_RESULT_OK;
}

void ScreenManager::flush_requested_page() {
    page_switch_scheduled_ = false;

    if (has_loaded_page_ && requested_page_ == loaded_page_) {
        return;
    }

    show_page(requested_page_);
}

lv_obj_t* ScreenManager::current_screen() const {
    return current_screen_ ? current_screen_->root() : nullptr;
}

void ScreenManager::load_screen(std::unique_ptr<screen::BaseScreen> next_screen) {
    if (!next_screen || !next_screen->root()) {
        return;
    }

    lv_screen_load(next_screen->root());
    current_screen_ = std::move(next_screen);
}

} // namespace app
