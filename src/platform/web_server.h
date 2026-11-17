/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace platform {

struct WebServerConfig {
    /* The folder the browser sees as its top level. Nothing outside it can be reached. */
    std::string root;
    /* First port tried; then the next nine, then any free port the system picks. */
    uint16_t port{8080};
};

/*
 * A small HTTP file manager for the browser: list, view, download, upload,
 * new folder, rename and delete, all inside one root folder, plus installing
 * a .deb from that folder with apt-get. A browser logs in
 * with the PIN shown on the device, which gives it a session cookie.
 *
 * The server runs on threads of its own; the UI thread polls status().
 */
class WebServer {
public:
    struct Status {
        bool running{false};
        uint16_t port{0};
        /* Why the server is not running. */
        std::string error;
        /* The root folder, as served. */
        std::string root;
        /* Six digits, new every start and on new_pin(). */
        std::string pin;
        /* The latest thing a browser did, e.g. "Uploaded remote.sub". */
        std::string last_event;
        /* Browsers logged in right now. */
        uint32_t sessions{0};
        /* A .deb uploaded through the page is being installed with apt-get. */
        bool installing{false};
    };

    /* Binds the port and starts serving. Check status().running for the result. */
    static std::unique_ptr<WebServer> start(const WebServerConfig& config);

    /* Stops the server and waits for its threads. */
    ~WebServer();

    WebServer(const WebServer&) = delete;
    WebServer& operator=(const WebServer&) = delete;

    Status status() const;
    /* Changes whenever status() does. */
    uint32_t revision() const;

    /* Picks a new PIN and logs every browser out. */
    void new_pin();

    struct Impl;

private:
    explicit WebServer(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

/* IPv4 addresses of the network interfaces that are up, loopback left out. */
std::vector<std::string> local_ipv4_addresses();

} // namespace platform
