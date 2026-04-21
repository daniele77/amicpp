// Copyright (c) 2026 Daniele Pallastrelli
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE or copy at
//  http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <amicpp/ami_client.hpp>

#include <condition_variable>
#include <mutex>
#include <string>

namespace amicpp {

class AmiSession {
public:
    AmiSession(
        AmiClient& client,
        std::string username,
        std::string secret,
        std::string events = "on");

    ~AmiSession() noexcept;

    AmiSession(const AmiSession&) = delete;
    AmiSession& operator=(const AmiSession&) = delete;

    AmiSession(AmiSession&& other) noexcept;
    AmiSession& operator=(AmiSession&& other) noexcept;

    bool logged_in() const noexcept;

private:
    AmiClient* client_;
    bool logged_in_;
};

} // namespace amicpp
