// Copyright (c) 2026 Daniele Pallastrelli
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE or copy at
//  http://www.boost.org/LICENSE_1_0.txt)

#include <amicpp/ami_session.hpp>

#include <stdexcept>
#include <utility>

namespace amicpp {

AmiSession::AmiSession(
    AmiClient& client,
    std::string username,
    std::string secret,
    std::string events)
    : client_(&client),
      logged_in_(false) {
    
    std::mutex login_mutex;
    std::condition_variable login_cv;
    bool login_done = false;
    bool login_success = false;
    std::string login_error;

    AmiMessage login;
    login.set("Action", "Login");
    login.set("Username", std::move(username));
    login.set("Secret", std::move(secret));
    login.set("Events", std::move(events));

    client_->async_send_action(std::move(login), [&login_mutex, &login_cv, &login_done, &login_success, &login_error](bool success, const AmiMessage& response) {
        std::lock_guard<std::mutex> lock(login_mutex);
        login_done = true;
        if (success) {
            const auto status = response.get("Response");
            if (status == "Success") {
                login_success = true;
            } else {
                login_error = "AMI login failed: " + response.get("Message", "unknown error");
            }
        } else {
            login_error = "AMI login action failed";
        }
        login_cv.notify_one();
    });

    std::unique_lock<std::mutex> lock(login_mutex);
    login_cv.wait(lock, [&login_done] { return login_done; });

    if (!login_success) {
        throw std::runtime_error(login_error.empty() ? "AMI login timeout" : login_error);
    }

    logged_in_ = true;
}

AmiSession::~AmiSession() noexcept {
    if (!client_ || !logged_in_ || !client_->is_connected()) {
        return;
    }

    std::mutex logoff_mutex;
    std::condition_variable logoff_cv;
    bool logoff_done = false;

    AmiMessage logoff;
    logoff.set("Action", "Logoff");

    client_->async_send_action(std::move(logoff), [&logoff_mutex, &logoff_cv, &logoff_done](bool, const AmiMessage&) {
        std::lock_guard<std::mutex> lock(logoff_mutex);
        logoff_done = true;
        logoff_cv.notify_one();
    });

    std::unique_lock<std::mutex> lock(logoff_mutex);
    logoff_cv.wait_for(lock, std::chrono::milliseconds(2000), [&logoff_done] { return logoff_done; });

    logged_in_ = false;
}

AmiSession::AmiSession(AmiSession&& other) noexcept
    : client_(other.client_),
      logged_in_(other.logged_in_) {
    other.client_ = nullptr;
    other.logged_in_ = false;
}

AmiSession& AmiSession::operator=(AmiSession&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    client_ = other.client_;
    logged_in_ = other.logged_in_;

    other.client_ = nullptr;
    other.logged_in_ = false;

    return *this;
}

bool AmiSession::logged_in() const noexcept {
    return logged_in_;
}

} // namespace amicpp
