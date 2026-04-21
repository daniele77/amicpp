// Copyright (c) 2026 Daniele Pallastrelli
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE or copy at
//  http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <amicpp/ami_message.hpp>
#include <amicpp/tcp_client.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace amicpp {

class AmiClient {
public:
    using EventHandler = std::function<void(const AmiMessage&)>;
    using ConnectHandler = std::function<void(const std::string&)>;
    using ActionHandler = std::function<void(bool, const AmiMessage&)>;

    explicit AmiClient(boost::asio::io_context& io_context);
    ~AmiClient();

    AmiClient(const AmiClient&) = delete;
    AmiClient& operator=(const AmiClient&) = delete;

    void connect(const std::string& host, const std::string& port = "5038");
    void disconnect();

    void async_connect(const std::string& host, const std::string& port, ConnectHandler handler);
    void async_disconnect();

    bool is_connected() const;
    const std::string& banner() const noexcept;

    std::string add_event_handler(EventHandler handler);
    bool remove_event_handler(const std::string& handler_id);

    AmiMessage send_action(
        AmiMessage action,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    void async_send_action(AmiMessage action, ActionHandler handler);
    void async_send_action(
        AmiMessage action,
        ActionHandler handler,
        std::chrono::milliseconds timeout);

private:
    struct PendingAction {
        std::atomic<bool> completed{false};
        ActionHandler handler;
        std::shared_ptr<boost::asio::steady_timer> timer;
    };

    std::string next_action_id();
    bool is_error_connect_result(const std::string& result) const;
    AmiMessage make_error_response(const std::string& message) const;
    void start_read_loop();
    void fail_all_pending(const std::string& reason);
    void route_message(const AmiMessage& message);

    boost::asio::io_context& io_context_;
    TcpClient tcp_client_;
    std::atomic<bool> connected_;
    std::string banner_;

    std::atomic<std::uint64_t> next_action_id_;

    mutable std::mutex handlers_mutex_;
    std::unordered_map<std::string, EventHandler> handlers_;

    mutable std::mutex pending_mutex_;
    std::unordered_map<std::string, std::shared_ptr<PendingAction>> pending_;
};

} // namespace amicpp
