/*
 * SPDX-License-Identifier: MIT
 */

#include "base_viewmodel.h"

#include "capture_store.h"

namespace viewmodel {
namespace {

/* Status and addresses are checked this often while the page is open. */
constexpr uint32_t kWebPollPeriodMs = 500;
/* Network addresses change rarely; look them up every few polls only. */
constexpr uint32_t kWebAddressEveryPolls = 4;

} // namespace

lv_subject_t* BaseViewModel::web_revision_subject() {
    return web_revision_subject_.native();
}

const platform::WebServer::Status& BaseViewModel::web_status() const {
    return web_status_;
}

const std::vector<std::string>& BaseViewModel::web_urls() const {
    return web_urls_;
}

void BaseViewModel::configure_web(uint16_t port) {
    web_port_ = port;
}

void BaseViewModel::web_new_pin() {
    if (web_server_ && web_status_.running) {
        web_server_->new_pin();
        poll_web();
    }
}

void BaseViewModel::sync_web_activity() {
    if (model_.current_page() != model::AppPage::WebFiles) {
        /* Leaving the page stops the server and logs every browser out. */
        web_server_.reset();
        web_status_ = platform::WebServer::Status{};
        web_urls_.clear();
        if (web_timer_) {
            lv_timer_pause(web_timer_);
        }
        return;
    }

    if (!web_server_) {
        platform::WebServerConfig config;
        config.root = platform::user_home_directory();
        config.port = web_port_;
        web_server_ = platform::WebServer::start(config);
        web_seen_revision_ = web_server_->revision() - 1;
    }
    if (!web_timer_) {
        web_timer_ = lv_timer_create(web_timer_cb, kWebPollPeriodMs, this);
    }
    else {
        lv_timer_resume(web_timer_);
    }
    web_urls_.clear();
    poll_web();
}

void BaseViewModel::poll_web() {
    if (!web_server_) {
        return;
    }
    bool changed = false;

    static uint32_t polls = 0;
    if (web_urls_.empty() || ++polls % kWebAddressEveryPolls == 0) {
        std::vector<std::string> urls;
        const auto port = web_server_->status().port;
        for (const auto& address : platform::local_ipv4_addresses()) {
            urls.push_back("http://" + address + ":" + std::to_string(port));
        }
        if (urls != web_urls_) {
            web_urls_ = std::move(urls);
            changed = true;
        }
    }

    const uint32_t revision = web_server_->revision();
    if (revision != web_seen_revision_) {
        web_seen_revision_ = revision;
        web_status_ = web_server_->status();
        changed = true;
    }

    if (changed) {
        web_revision_subject_.set(web_revision_subject_.value() + 1);
    }
}

void BaseViewModel::web_timer_cb(lv_timer_t* timer) {
    auto* view_model = static_cast<BaseViewModel*>(lv_timer_get_user_data(timer));
    if (view_model) {
        view_model->poll_web();
    }
}

} // namespace viewmodel
