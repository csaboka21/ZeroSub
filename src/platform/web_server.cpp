/*
 * SPDX-License-Identifier: MIT
 */

#include "web_server.h"

#include "capture_store.h"
#include "web_page.h"

/* A handful of threads is plenty for one person with a browser. */
#define CPPHTTPLIB_THREAD_POOL_COUNT 4
#include <httplib.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <thread>
#include <unordered_set>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

extern char** environ;
#endif

namespace platform {
namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

constexpr int kPortTries = 10;
constexpr int kMaxFailedLogins = 5;
constexpr auto kLoginLockout = std::chrono::seconds(30);
constexpr std::size_t kMaxSessions = 16;
constexpr const char* kSessionCookie = "zs_session";
/* Uploads are streamed to disk, so the only limit is the card. */
constexpr std::size_t kMaxPayload = static_cast<std::size_t>(64) * 1024 * 1024 * 1024;
/* Headers every response gets. */
constexpr const char* kPageCsp =
    "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; "
    "img-src 'self' blob:; connect-src 'self'; frame-src 'self'; form-action 'self'; "
    "frame-ancestors 'none'; base-uri 'none'";

std::string random_digits(std::size_t count) {
    std::random_device device;
    std::uniform_int_distribution<int> digit(0, 9);
    std::string text;
    for (std::size_t index = 0; index < count; ++index) {
        text += static_cast<char>('0' + digit(device));
    }
    return text;
}

std::string random_token() {
    static constexpr char kHex[] = "0123456789abcdef";
    std::random_device device;
    std::uniform_int_distribution<int> nibble(0, 15);
    std::string text;
    for (int index = 0; index < 40; ++index) {
        text += kHex[nibble(device)];
    }
    return text;
}

bool equal_constant_time(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
        return false;
    }
    unsigned char diff = 0;
    for (std::size_t index = 0; index < a.size(); ++index) {
        diff |= static_cast<unsigned char>(a[index] ^ b[index]);
    }
    return diff == 0;
}

std::string json_escape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 2);
    for (const char c : text) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(c));
                    out += buffer;
                }
                else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

/* RFC 5987 value for filename*= in Content-Disposition. */
std::string percent_encode(const std::string& text) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if ((byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') ||
            byte == '-' || byte == '.' || byte == '_' || byte == '~') {
            out += c;
        }
        else {
            out += '%';
            out += kHex[byte >> 4];
            out += kHex[byte & 0x0F];
        }
    }
    return out;
}

/* A plain ASCII stand-in for filename= in Content-Disposition. */
std::string ascii_filename(const std::string& text) {
    std::string out;
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        out += (byte >= 0x20 && byte < 0x7F && c != '"' && c != '\\') ? c : '_';
    }
    return out;
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

/*
 * Content type for viewing a file in the browser, or empty when it should only
 * be downloaded. HTML and SVG are shown as text so a file can never run script
 * on the page's origin.
 */
std::string inline_content_type(const fs::path& path) {
    const std::string ext = lower(path.extension().string());
    if (ext == ".png") {
        return "image/png";
    }
    if (ext == ".jpg" || ext == ".jpeg") {
        return "image/jpeg";
    }
    if (ext == ".gif") {
        return "image/gif";
    }
    if (ext == ".bmp") {
        return "image/bmp";
    }
    if (ext == ".webp") {
        return "image/webp";
    }
    static const char* const kText[] = {
        ".sub", ".ir",  ".nfc",  ".rfid", ".txt", ".log", ".conf", ".cfg", ".ini", ".md",   ".json", ".csv",
        ".xml", ".yml", ".yaml", ".sh",   ".py",  ".c",   ".h",    ".cpp", ".js",  ".html", ".htm",  ".svg",
        ".css", ".fap", ".u2f",  ".ibtn", ".lua", ".service", ".rules", ".list",
    };
    for (const char* text_ext : kText) {
        if (ext == text_ext) {
            return "text/plain; charset=utf-8";
        }
    }
    /* Dotfiles such as .bashrc have no extension but are text. */
    if (ext.empty()) {
        return "text/plain; charset=utf-8";
    }
    return "";
}

/* One path component a browser may create or rename to. */
bool valid_name(const std::string& name) {
    if (name.empty() || name == "." || name == ".." || name.size() > 255) {
        return false;
    }
    return name.find('/') == std::string::npos && name.find('\0') == std::string::npos;
}

/* Splits "a/b/c" into components, refusing "..". Empty and "." parts are skipped. */
bool split_relative(const std::string& text, std::vector<std::string>& parts) {
    parts.clear();
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('/', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string part = text.substr(start, end - start);
        if (part == "..") {
            return false;
        }
        if (!part.empty() && part != ".") {
            if (!valid_name(part)) {
                return false;
            }
            parts.push_back(part);
        }
        start = end + 1;
    }
    return true;
}

std::string join(const std::vector<std::string>& parts) {
    std::string text;
    for (const auto& part : parts) {
        if (!text.empty()) {
            text += '/';
        }
        text += part;
    }
    return text;
}

struct EntryInfo {
    bool directory{false};
    bool link{false};
    uint64_t size{0};
    int64_t mtime{0};
};

bool entry_info(const fs::path& path, EntryInfo& info) {
#if !defined(_WIN32)
    struct stat link_stat {};
    if (::lstat(path.c_str(), &link_stat) != 0) {
        return false;
    }
    info.link = S_ISLNK(link_stat.st_mode);
    struct stat target_stat {};
    const struct stat& st = (info.link && ::stat(path.c_str(), &target_stat) == 0) ? target_stat : link_stat;
    info.directory = S_ISDIR(st.st_mode);
    info.size = S_ISREG(st.st_mode) ? static_cast<uint64_t>(st.st_size) : 0;
    info.mtime = static_cast<int64_t>(st.st_mtime);
    return true;
#else
    std::error_code ec;
    const auto status = fs::status(path, ec);
    if (ec) {
        return false;
    }
    info.link = fs::is_symlink(fs::symlink_status(path, ec));
    info.directory = fs::is_directory(status);
    info.size = fs::is_regular_file(status) ? static_cast<uint64_t>(fs::file_size(path, ec)) : 0;
    info.mtime = 0;
    return true;
#endif
}

void send_json(httplib::Response& res, const std::string& body, int status = 200) {
    res.status = status;
    res.set_content(body, "application/json");
}

void send_error(httplib::Response& res, int status, const std::string& message) {
    send_json(res, "{\"error\":\"" + json_escape(message) + "\"}", status);
}

void send_ok(httplib::Response& res) {
    send_json(res, "{\"ok\":true}");
}

std::string session_cookie(const httplib::Request& req) {
    const std::string header = req.get_header_value("Cookie");
    const std::string key = std::string(kSessionCookie) + "=";
    std::size_t start = 0;
    while (start < header.size()) {
        while (start < header.size() && (header[start] == ' ' || header[start] == ';')) {
            ++start;
        }
        std::size_t end = header.find(';', start);
        if (end == std::string::npos) {
            end = header.size();
        }
        if (header.compare(start, key.size(), key) == 0) {
            return header.substr(start + key.size(), end - start - key.size());
        }
        start = end + 1;
    }
    return "";
}

/* Orders events from the server and from the install job, so status() shows the newest. */
std::atomic<uint64_t> g_event_sequence{0};

/*
 * The one package install that can run at a time. It outlives any server: apt
 * keeps going when the page is left, and its result is shown when it is back.
 */
struct InstallJob {
    std::mutex mutex;
    bool running{false};
    bool finished{false};
    int exit_code{-1};
    std::string package;
    std::string event;
    uint64_t event_sequence{0};
    std::atomic<uint32_t> revision{0};
};

InstallJob& install_job() {
    static InstallJob job;
    return job;
}

/*
 * apt writes to a file rather than a pipe, so it is never killed by SIGPIPE if
 * ZeroSub exits mid-install. One file per user: in sticky /tmp a user cannot
 * replace another user's file, so a root run and a plain run must not share it.
 */
std::string install_log_path() {
#if defined(_WIN32)
    return "zerosub-install.log";
#else
    return "/tmp/zerosub-install-" + std::to_string(::geteuid()) + ".log";
#endif
}
/* The page shows this much of the end of the log. */
constexpr std::size_t kInstallLogTail = 64 * 1024;

std::string read_install_log_tail() {
    std::ifstream input(install_log_path(), std::ios::binary);
    if (!input) {
        return "";
    }
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    const std::streamoff start = size > static_cast<std::streamoff>(kInstallLogTail)
                                     ? size - static_cast<std::streamoff>(kInstallLogTail)
                                     : 0;
    input.seekg(start);
    std::string text(static_cast<std::size_t>(size - start), '\0');
    input.read(&text[0], static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(input.gcount()));
    return text;
}

/*
 * Starts `apt-get install` on a local .deb, as root: directly when ZeroSub
 * runs as root, otherwise through
 * `sudo -n`, which fails rather than asking for a password.
 */
bool start_install(const fs::path& deb, std::string& error) {
#if defined(_WIN32)
    (void)deb;
    error = "Installing packages needs Linux";
    return false;
#else
    auto& job = install_job();
    {
        std::lock_guard<std::mutex> lock(job.mutex);
        if (job.running) {
            error = "Another install is still running";
            return false;
        }
    }

    std::vector<std::string> args;
    if (::geteuid() != 0) {
        args = {"/usr/bin/sudo", "-n"};
    }
    const std::vector<std::string> apt = {
        "/usr/bin/apt-get", "install", "-y", "--reinstall", "--allow-downgrades", "--no-install-recommends",
        "-o", "Dpkg::Options::=--force-confdef", "-o", "Dpkg::Options::=--force-confold", deb.string(),
    };
    args.insert(args.end(), apt.begin(), apt.end());

    /* Everything the child needs is prepared here: after fork() only async-signal-safe calls are made. */
    std::vector<std::string> env_strings;
    for (char** entry = environ; entry && *entry; ++entry) {
        const std::string value = *entry;
        if (value.rfind("DEBIAN_FRONTEND=", 0) != 0 && value.rfind("PATH=", 0) != 0) {
            env_strings.push_back(value);
        }
    }
    env_strings.emplace_back("DEBIAN_FRONTEND=noninteractive");
    env_strings.emplace_back("PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin");
    std::vector<char*> argv;
    for (auto& arg : args) {
        argv.push_back(&arg[0]);
    }
    argv.push_back(nullptr);
    std::vector<char*> envp;
    for (auto& value : env_strings) {
        envp.push_back(&value[0]);
    }
    envp.push_back(nullptr);

    /*
     * Start from a fresh file. Removing first and then creating with O_EXCL
     * means a file or symlink someone planted under this name is never written
     * through (fs.protected_regular would refuse to open it anyway).
     */
    const std::string log_path = install_log_path();
    ::unlink(log_path.c_str());
    const int log_fd = ::open(log_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (log_fd < 0) {
        error = "Cannot write " + log_path + ": " + std::strerror(errno);
        return false;
    }
    std::string header = "$";
    for (const auto& arg : args) {
        header += " " + arg;
    }
    header += "\n\n";
    if (::write(log_fd, header.data(), header.size()) < 0) {
        /* Not fatal: the log just lacks its first line. */
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(log_fd);
        error = "Could not start apt-get";
        return false;
    }
    if (pid == 0) {
        /* A session of its own: apt finishes even if ZeroSub is stopped or its terminal goes away. */
        ::setsid();
        ::dup2(log_fd, STDOUT_FILENO);
        ::dup2(log_fd, STDERR_FILENO);
        const int null_fd = ::open("/dev/null", O_RDONLY);
        if (null_fd >= 0) {
            ::dup2(null_fd, STDIN_FILENO);
        }
        /* Leave the server's sockets and the app's devices behind. */
        for (int fd = 3; fd < 1024; ++fd) {
            ::close(fd);
        }
        ::execve(argv[0], argv.data(), envp.data());
        static const char kExecFailed[] = "Could not run apt-get\n";
        if (::write(STDERR_FILENO, kExecFailed, sizeof(kExecFailed) - 1) < 0) {
        }
        ::_exit(127);
    }
    ::close(log_fd);

    const std::string name = deb.filename().string();
    {
        std::lock_guard<std::mutex> lock(job.mutex);
        job.running = true;
        job.finished = false;
        job.exit_code = -1;
        job.package = name;
        job.event = "Installing " + name + "...";
        job.event_sequence = ++g_event_sequence;
    }
    job.revision.fetch_add(1);

    std::thread([pid, name] {
        int status = 0;
        pid_t result = 0;
        do {
            result = ::waitpid(pid, &status, 0);
        } while (result < 0 && errno == EINTR);
        int code = -1;
        if (result == pid) {
            code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
        }
        auto& finished_job = install_job();
        {
            std::lock_guard<std::mutex> lock(finished_job.mutex);
            finished_job.running = false;
            finished_job.finished = true;
            finished_job.exit_code = code;
            finished_job.event = code == 0 ? "Installed " + name : "Install failed: " + name;
            finished_job.event_sequence = ++g_event_sequence;
        }
        finished_job.revision.fetch_add(1);
    }).detach();
    return true;
#endif
}

} // namespace

struct WebServer::Impl {
    /* Replaced on every bind attempt: httplib gives up on a server whose bind failed once. */
    std::unique_ptr<httplib::Server> server;
    std::thread thread;
    fs::path root;

    mutable std::mutex mutex;
    Status status;
    std::unordered_set<std::string> sessions;
    int failed_logins{0};
    Clock::time_point locked_until{};
    std::atomic<uint32_t> revision{0};
    /* When status.last_event was set, in g_event_sequence order. */
    uint64_t event_sequence{0};

    void touch() {
        revision.fetch_add(1);
    }

    void event(const std::string& text) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            status.last_event = text;
            event_sequence = ++g_event_sequence;
        }
        touch();
    }

    bool authorized(const httplib::Request& req) const {
        const std::string token = session_cookie(req);
        if (token.empty()) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex);
        return sessions.count(token) != 0;
    }

    /*
     * A browser path (relative to the root, "/"-separated) as a path on disk.
     * Purely lexical: ".." is refused, symlinks are not followed. `normal` is
     * the cleaned-up relative path.
     */
    bool resolve(const std::string& relative, fs::path& path, std::string* normal = nullptr) const {
        std::vector<std::string> parts;
        if (!split_relative(relative, parts)) {
            return false;
        }
        path = root;
        for (const auto& part : parts) {
            path /= part;
        }
        if (normal) {
            *normal = join(parts);
        }
        return true;
    }

    /* True when the path, symlinks followed, is the root or below it. */
    bool inside_root(const fs::path& path) const {
        std::error_code ec;
        const fs::path real = fs::weakly_canonical(path, ec);
        if (ec) {
            return false;
        }
        auto r = root.begin();
        auto p = real.begin();
        for (; r != root.end(); ++r, ++p) {
            if (p == real.end() || *r != *p) {
                return false;
            }
        }
        return true;
    }

    /* Creates the folders of `relative` below `base` one by one, handing each new one to the user. */
    bool make_directories(const fs::path& base, const std::vector<std::string>& relative, std::string& error) const {
        fs::path path = base;
        for (const auto& part : relative) {
            path /= part;
            std::error_code ec;
            if (fs::is_directory(path, ec)) {
                if (!inside_root(path)) {
                    error = "Folder leads outside the home folder";
                    return false;
                }
                continue;
            }
            if (!fs::create_directory(path, ec) || ec) {
                error = "Could not create folder " + part + (ec ? ": " + ec.message() : "");
                return false;
            }
            hand_to_invoking_user(path.string());
        }
        return true;
    }

    void routes();
    void handle_login(const httplib::Request& req, httplib::Response& res);
    void handle_list(const httplib::Request& req, httplib::Response& res);
    void handle_file(const httplib::Request& req, httplib::Response& res);
    void handle_upload(const httplib::Request& req, httplib::Response& res, const httplib::ContentReader& reader);
    void handle_mkdir(const httplib::Request& req, httplib::Response& res);
    void handle_rename(const httplib::Request& req, httplib::Response& res);
    void handle_delete(const httplib::Request& req, httplib::Response& res);
    void handle_install(const httplib::Request& req, httplib::Response& res);
};

void WebServer::Impl::routes() {
    server->set_payload_max_length(kMaxPayload);

    server->set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr ep) {
        std::string message = "Internal error";
        try {
            std::rethrow_exception(ep);
        }
        catch (const std::exception& e) {
            message = e.what();
        }
        catch (...) {
        }
        send_error(res, 500, message);
    });

    server->set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
        res.set_header("X-Content-Type-Options", "nosniff");
        res.set_header("Cache-Control", "no-store");
        res.set_header("Referrer-Policy", "no-referrer");
        /*
         * Every POST must carry this header. A page on another site cannot add
         * it without a CORS preflight, which this server never answers.
         */
        if (req.method == "POST" && req.get_header_value("X-ZeroSub") != "1") {
            send_error(res, 403, "Missing request header");
            return httplib::Server::HandlerResponse::Handled;
        }
        if (req.path == "/" || req.path == "/api/login") {
            return httplib::Server::HandlerResponse::Unhandled;
        }
        if (!authorized(req)) {
            send_error(res, 401, "Log in with the PIN shown on the device");
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    server->Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Content-Security-Policy", kPageCsp);
        res.set_header("X-Frame-Options", "DENY");
        res.set_content(kWebPageHtml, "text/html; charset=utf-8");
    });

    server->Post("/api/login", [this](const httplib::Request& req, httplib::Response& res) {
        handle_login(req, res);
    });

    server->Post("/api/logout", [this](const httplib::Request& req, httplib::Response& res) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            sessions.erase(session_cookie(req));
            status.sessions = static_cast<uint32_t>(sessions.size());
        }
        touch();
        res.set_header("Set-Cookie", std::string(kSessionCookie) + "=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
        send_ok(res);
    });

    server->Get("/api/info", [this](const httplib::Request&, httplib::Response& res) {
        send_json(res, "{\"root\":\"" + json_escape(root.string()) + "\"}");
    });

    server->Get("/api/list", [this](const httplib::Request& req, httplib::Response& res) {
        handle_list(req, res);
    });
    server->Get("/api/file", [this](const httplib::Request& req, httplib::Response& res) {
        handle_file(req, res);
    });
    server->Post("/api/upload",
                [this](const httplib::Request& req, httplib::Response& res, const httplib::ContentReader& reader) {
                    handle_upload(req, res, reader);
                });
    server->Post("/api/mkdir", [this](const httplib::Request& req, httplib::Response& res) {
        handle_mkdir(req, res);
    });
    server->Post("/api/rename", [this](const httplib::Request& req, httplib::Response& res) {
        handle_rename(req, res);
    });
    server->Post("/api/delete", [this](const httplib::Request& req, httplib::Response& res) {
        handle_delete(req, res);
    });
    server->Post("/api/install", [this](const httplib::Request& req, httplib::Response& res) {
        handle_install(req, res);
    });
    server->Get("/api/install", [](const httplib::Request&, httplib::Response& res) {
        auto& job = install_job();
        std::string body;
        {
            std::lock_guard<std::mutex> lock(job.mutex);
            body = std::string("{\"running\":") + (job.running ? "true" : "false") +
                   ",\"finished\":" + (job.finished ? "true" : "false") +
                   ",\"exit\":" + std::to_string(job.exit_code) +
                   ",\"package\":\"" + json_escape(job.package) + "\"";
        }
        body += ",\"log\":\"" + json_escape(read_install_log_tail()) + "\"}";
        send_json(res, body);
    });
}

void WebServer::Impl::handle_login(const httplib::Request& req, httplib::Response& res) {
    const std::string pin = req.get_param_value("pin");
    std::string token;
    {
        std::lock_guard<std::mutex> lock(mutex);
        const auto now = Clock::now();
        if (now < locked_until) {
            const auto wait = std::chrono::duration_cast<std::chrono::seconds>(locked_until - now).count() + 1;
            send_error(res, 429, "Too many wrong PINs: wait " + std::to_string(wait) + " s");
            return;
        }
        if (!equal_constant_time(pin, status.pin)) {
            if (++failed_logins >= kMaxFailedLogins) {
                failed_logins = 0;
                locked_until = now + kLoginLockout;
            }
            status.last_event = "Wrong PIN from " + req.remote_addr;
            event_sequence = ++g_event_sequence;
        }
        else {
            failed_logins = 0;
            if (sessions.size() >= kMaxSessions) {
                sessions.erase(sessions.begin());
            }
            token = random_token();
            sessions.insert(token);
            status.sessions = static_cast<uint32_t>(sessions.size());
            status.last_event = "Logged in: " + req.remote_addr;
            event_sequence = ++g_event_sequence;
        }
    }
    touch();
    if (token.empty()) {
        send_error(res, 403, "Wrong PIN");
        return;
    }
    res.set_header("Set-Cookie", std::string(kSessionCookie) + "=" + token + "; Path=/; HttpOnly; SameSite=Strict");
    send_ok(res);
}

void WebServer::Impl::handle_list(const httplib::Request& req, httplib::Response& res) {
    fs::path dir;
    std::string normal;
    if (!resolve(req.get_param_value("path"), dir, &normal) || !inside_root(dir)) {
        send_error(res, 400, "Path is outside the home folder");
        return;
    }
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        send_error(res, 404, "Not a folder");
        return;
    }
    fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        send_error(res, 403, "Cannot open folder: " + ec.message());
        return;
    }

    std::string body = "{\"path\":\"" + json_escape(normal) + "\",\"entries\":[";
    bool first = true;
    for (; it != fs::directory_iterator(); it.increment(ec)) {
        if (ec) {
            break;
        }
        EntryInfo info;
        if (!entry_info(it->path(), info)) {
            continue;
        }
        if (!first) {
            body += ',';
        }
        first = false;
        body += "{\"name\":\"" + json_escape(it->path().filename().string()) + "\"";
        body += std::string(",\"dir\":") + (info.directory ? "true" : "false");
        body += std::string(",\"link\":") + (info.link ? "true" : "false");
        body += ",\"size\":" + std::to_string(info.size);
        body += ",\"mtime\":" + std::to_string(info.mtime) + "}";
    }
    body += "]}";
    send_json(res, body);
}

void WebServer::Impl::handle_file(const httplib::Request& req, httplib::Response& res) {
    fs::path path;
    if (!resolve(req.get_param_value("path"), path) || !inside_root(path)) {
        send_error(res, 400, "Path is outside the home folder");
        return;
    }
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        send_error(res, 404, "Not a file");
        return;
    }
    const std::string name = path.filename().string();
    const bool want_inline = req.get_param_value("inline") == "1";
    const std::string inline_type = want_inline ? inline_content_type(path) : std::string();
    /* Whatever the file holds, it gets no script, forms or same-origin access. */
    res.set_header("Content-Security-Policy", "sandbox");
    if (!inline_type.empty()) {
        res.set_header("Content-Disposition", "inline; filename*=UTF-8''" + percent_encode(name));
        res.set_file_content(path.string(), inline_type);
        return;
    }
    res.set_header("Content-Disposition",
                   "attachment; filename=\"" + ascii_filename(name) + "\"; filename*=UTF-8''" + percent_encode(name));
    res.set_file_content(path.string(), "application/octet-stream");
    event("Downloaded " + name);
}

void WebServer::Impl::handle_upload(const httplib::Request& req,
                                    httplib::Response& res,
                                    const httplib::ContentReader& reader) {
    fs::path dir;
    if (!resolve(req.get_param_value("path"), dir) || !inside_root(dir)) {
        send_error(res, 400, "Path is outside the home folder");
        return;
    }
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        send_error(res, 404, "Not a folder");
        return;
    }
    if (!req.is_multipart_form_data()) {
        send_error(res, 400, "Expected a multipart upload");
        return;
    }

    /*
     * The page sends each file as a "path" field (its path relative to the
     * folder, for folder uploads) followed by the "file" part. Files are
     * written to a temporary name and renamed once complete.
     */
    std::string error;
    std::string pending_path;
    bool reading_path = false;
    std::ofstream out;
    fs::path temp_path;
    fs::path target_path;
    bool writing = false;
    int saved = 0;
    std::string last_name;

    auto finish_file = [&](bool complete) {
        if (!writing) {
            return;
        }
        writing = false;
        out.close();
        std::error_code remove_ec;
        if (complete && !out.fail()) {
            std::error_code rename_ec;
            fs::rename(temp_path, target_path, rename_ec);
            if (!rename_ec) {
                hand_to_invoking_user(target_path.string());
                ++saved;
                return;
            }
            error = "Could not save " + target_path.filename().string() + ": " + rename_ec.message();
        }
        fs::remove(temp_path, remove_ec);
    };

    const bool read_ok = reader(
        [&](const httplib::FormData& part) {
            finish_file(true);
            if (!error.empty()) {
                return false;
            }
            reading_path = false;
            if (part.filename.empty()) {
                if (part.name == "path") {
                    reading_path = true;
                    pending_path.clear();
                }
                return true;
            }

            const std::string relative = pending_path.empty() ? part.filename : pending_path;
            pending_path.clear();
            std::vector<std::string> parts;
            if (!split_relative(relative, parts) || parts.empty()) {
                error = "Bad file name: " + relative;
                return false;
            }
            const std::string file_name = parts.back();
            parts.pop_back();
            if (!make_directories(dir, parts, error)) {
                return false;
            }
            fs::path target = dir;
            for (const auto& part_name : parts) {
                target /= part_name;
            }
            target /= file_name;
            std::error_code check_ec;
            if (!inside_root(target.parent_path()) || fs::is_directory(target, check_ec) ||
                fs::is_symlink(fs::symlink_status(target, check_ec))) {
                error = "Cannot write " + file_name + ": a folder or link has that name";
                return false;
            }
            target_path = target;
            temp_path = target;
            temp_path += ".zsupload";
            out.open(temp_path, std::ios::binary | std::ios::trunc);
            if (!out) {
                error = "Cannot write " + file_name + ": permission denied";
                return false;
            }
            writing = true;
            last_name = file_name;
            return true;
        },
        [&](const char* data, std::size_t length) {
            if (reading_path) {
                if (pending_path.size() + length > 4096) {
                    error = "Path too long";
                    return false;
                }
                pending_path.append(data, length);
                return true;
            }
            if (!writing) {
                return true;
            }
            out.write(data, static_cast<std::streamsize>(length));
            if (!out) {
                error = "Write failed: is the storage full?";
                return false;
            }
            return true;
        });
    finish_file(read_ok && error.empty());

    if (error.empty() && !read_ok) {
        error = "Upload interrupted";
    }
    if (saved > 0) {
        event(saved == 1 ? "Uploaded " + last_name : "Uploaded " + std::to_string(saved) + " files");
    }
    if (!error.empty()) {
        send_error(res, 500, error + (saved > 0 ? " (" + std::to_string(saved) + " saved)" : ""));
        return;
    }
    send_json(res, "{\"ok\":true,\"saved\":" + std::to_string(saved) + "}");
}

void WebServer::Impl::handle_mkdir(const httplib::Request& req, httplib::Response& res) {
    fs::path dir;
    const std::string name = req.get_param_value("name");
    if (!resolve(req.get_param_value("path"), dir) || !inside_root(dir)) {
        send_error(res, 400, "Path is outside the home folder");
        return;
    }
    if (!valid_name(name)) {
        send_error(res, 400, "Bad folder name");
        return;
    }
    const fs::path target = dir / name;
    std::error_code ec;
    if (fs::exists(fs::symlink_status(target, ec))) {
        send_error(res, 409, name + " already exists");
        return;
    }
    if (!fs::create_directory(target, ec) || ec) {
        send_error(res, 500, "Could not create folder" + (ec ? ": " + ec.message() : std::string()));
        return;
    }
    hand_to_invoking_user(target.string());
    event("New folder " + name);
    send_ok(res);
}

void WebServer::Impl::handle_rename(const httplib::Request& req, httplib::Response& res) {
    fs::path path;
    const std::string name = req.get_param_value("name");
    /* The entry itself is renamed, not what a symlink points to: only its folder has to be inside. */
    if (!resolve(req.get_param_value("path"), path) || path == root || !inside_root(path.parent_path())) {
        send_error(res, 400, "Cannot rename that");
        return;
    }
    if (!valid_name(name)) {
        send_error(res, 400, "Bad name");
        return;
    }
    std::error_code ec;
    if (!fs::exists(fs::symlink_status(path, ec))) {
        send_error(res, 404, "Not found");
        return;
    }
    const fs::path target = path.parent_path() / name;
    if (fs::exists(fs::symlink_status(target, ec))) {
        send_error(res, 409, name + " already exists");
        return;
    }
    fs::rename(path, target, ec);
    if (ec) {
        send_error(res, 500, "Rename failed: " + ec.message());
        return;
    }
    event("Renamed to " + name);
    send_ok(res);
}

void WebServer::Impl::handle_delete(const httplib::Request& req, httplib::Response& res) {
    fs::path path;
    /* remove_all() never follows symlinks, so deleting a link only removes the link. */
    if (!resolve(req.get_param_value("path"), path) || path == root || !inside_root(path.parent_path())) {
        send_error(res, 400, "Cannot delete that");
        return;
    }
    std::error_code ec;
    if (!fs::exists(fs::symlink_status(path, ec))) {
        send_error(res, 404, "Not found");
        return;
    }
    fs::remove_all(path, ec);
    if (ec) {
        send_error(res, 500, "Delete failed: " + ec.message());
        return;
    }
    event("Deleted " + path.filename().string());
    send_ok(res);
}

void WebServer::Impl::handle_install(const httplib::Request& req, httplib::Response& res) {
    fs::path path;
    if (!resolve(req.get_param_value("path"), path) || !inside_root(path)) {
        send_error(res, 400, "Path is outside the home folder");
        return;
    }
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || lower(path.extension().string()) != ".deb") {
        send_error(res, 400, "Only .deb files can be installed");
        return;
    }
    /* apt gets the real file, so a symlink cannot be swapped between this check and the install. */
    const fs::path real = fs::canonical(path, ec);
    if (ec) {
        send_error(res, 400, "Cannot open the package");
        return;
    }
    std::string error;
    if (!start_install(real, error)) {
        send_error(res, 409, error);
        return;
    }
    touch();
    send_ok(res);
}

WebServer::WebServer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

std::unique_ptr<WebServer> WebServer::start(const WebServerConfig& config) {
    auto impl = std::make_unique<Impl>();

    std::error_code ec;
    impl->root = fs::weakly_canonical(config.root.empty() ? fs::path(".") : fs::path(config.root), ec);
    impl->status.root = impl->root.string();
    impl->status.pin = random_digits(6);

    if (ec || !fs::is_directory(impl->root, ec)) {
        impl->status.error = "Folder not found: " + config.root;
        return std::unique_ptr<WebServer>(new WebServer(std::move(impl)));
    }

    /* The configured port, then the next nine, then whatever free port the system picks (0). */
    std::vector<int> ports;
    for (int attempt = 0; attempt < kPortTries && config.port + attempt <= 65535; ++attempt) {
        ports.push_back(static_cast<int>(config.port) + attempt);
    }
    ports.push_back(0);
    for (const int port : ports) {
        impl->server = std::make_unique<httplib::Server>();
        impl->routes();
        const int bound = port == 0 ? impl->server->bind_to_any_port("0.0.0.0")
                                    : (impl->server->bind_to_port("0.0.0.0", port) ? port : -1);
        if (bound > 0 && bound <= 65535) {
            impl->status.port = static_cast<uint16_t>(bound);
            break;
        }
    }
    if (impl->status.port == 0) {
        impl->server.reset();
        impl->status.error = "Could not open a network port";
        return std::unique_ptr<WebServer>(new WebServer(std::move(impl)));
    }

    impl->status.running = true;
    Impl* raw = impl.get();
    impl->thread = std::thread([raw] {
        raw->server->listen_after_bind();
        {
            std::lock_guard<std::mutex> lock(raw->mutex);
            raw->status.running = false;
        }
        raw->touch();
    });
    return std::unique_ptr<WebServer>(new WebServer(std::move(impl)));
}

WebServer::~WebServer() {
    if (!impl_) {
        return;
    }
    if (impl_->server) {
        impl_->server->stop();
    }
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }
}

WebServer::Status WebServer::status() const {
    Status result;
    uint64_t sequence = 0;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        result = impl_->status;
        sequence = impl_->event_sequence;
    }
    /* An install's progress or result wins over older server events. */
    auto& job = install_job();
    std::lock_guard<std::mutex> lock(job.mutex);
    result.installing = job.running;
    if (job.event_sequence > sequence) {
        result.last_event = job.event;
    }
    return result;
}

uint32_t WebServer::revision() const {
    return impl_->revision.load() + install_job().revision.load();
}

void WebServer::new_pin() {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->status.pin = random_digits(6);
        impl_->sessions.clear();
        impl_->status.sessions = 0;
        impl_->failed_logins = 0;
        impl_->status.last_event = "New PIN: browsers logged out";
        impl_->event_sequence = ++g_event_sequence;
    }
    impl_->touch();
}

std::vector<std::string> local_ipv4_addresses() {
    std::vector<std::string> addresses;
#if !defined(_WIN32)
    ifaddrs* list = nullptr;
    if (::getifaddrs(&list) != 0) {
        return addresses;
    }
    for (ifaddrs* entry = list; entry; entry = entry->ifa_next) {
        if (!entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        if ((entry->ifa_flags & IFF_UP) == 0 || (entry->ifa_flags & IFF_LOOPBACK) != 0) {
            continue;
        }
        char text[INET_ADDRSTRLEN] = {};
        const auto* address = reinterpret_cast<const sockaddr_in*>(entry->ifa_addr);
        if (::inet_ntop(AF_INET, &address->sin_addr, text, sizeof(text))) {
            addresses.emplace_back(text);
        }
    }
    ::freeifaddrs(list);
#endif
    return addresses;
}

} // namespace platform
