// Copyright (c) 2026 Daniele Pallastrelli
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE or copy at
//  http://www.boost.org/LICENSE_1_0.txt)

#include <amicpp/ami_session.hpp>

#include <chrono>
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
    AmiMessage login;
    login.set("Action", "Login");
    login.set("Username", std::move(username));
    login.set("Secret", std::move(secret));
    login.set("Events", std::move(events));

    const auto response = client_->send_action(std::move(login));
    const auto status = response.get("Response");

    if (status != "Success") {
        throw std::runtime_error(
            "AMI login failed: " + response.get("Message", "unknown error"));
    }

    logged_in_ = true;
}

AmiSession::~AmiSession() noexcept {
    if (!client_ || !logged_in_ || !client_->is_connected()) {
        return;
    }

    try {
        AmiMessage logoff;
        logoff.set("Action", "Logoff");
        (void)client_->send_action(std::move(logoff), std::chrono::milliseconds(2000));
    } catch (...) {
    }

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
