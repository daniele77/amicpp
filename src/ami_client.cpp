// Copyright (c) 2026 Daniele Pallastrelli
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE or copy at
//  http://www.boost.org/LICENSE_1_0.txt)

#include <amicpp/ami_client.hpp>

#include <stdexcept>

namespace amicpp {

AmiClient::AmiClient()
    : running_(false),
      next_action_id_(1) {}

AmiClient::~AmiClient() {
    disconnect();
}

void AmiClient::connect(const std::string& host, const std::string& port) {
    if (is_connected()) {
        return;
    }

    tcp_client_.connect(host, port);

    try {
        banner_ = tcp_client_.read_line();
    } catch (...) {
        banner_.clear();
    }

    running_.store(true);
    reader_thread_ = std::thread(&AmiClient::reader_loop, this);
}

void AmiClient::disconnect() {
    const bool was_running = running_.exchange(false);

    if (tcp_client_.is_connected()) {
        tcp_client_.disconnect();
    }

    if (reader_thread_.joinable()) {
        reader_thread_.join();
    }

    if (was_running) {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        for (auto& entry : pending_) {
            auto& pending = entry.second;
            {
                std::lock_guard<std::mutex> pending_lock(pending->mutex);
                pending->ready = true;
            }
            pending->cv.notify_all();
        }
        pending_.clear();
    }
}

bool AmiClient::is_connected() const {
    return tcp_client_.is_connected();
}

const std::string& AmiClient::banner() const noexcept {
    return banner_;
}

std::string AmiClient::add_event_handler(EventHandler handler) {
    const auto id = std::string("handler-") + std::to_string(next_action_id_.fetch_add(1));
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    handlers_.emplace(id, std::move(handler));
    return id;
}

bool AmiClient::remove_event_handler(const std::string& handler_id) {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    return handlers_.erase(handler_id) > 0;
}

AmiMessage AmiClient::send_action(AmiMessage action, std::chrono::milliseconds timeout) {
    if (!is_connected()) {
        throw std::runtime_error("AMI client is not connected");
    }

    if (!action.has("Action")) {
        throw std::invalid_argument("AMI action must include the 'Action' field");
    }

    if (!action.has("ActionID")) {
        action.set("ActionID", next_action_id());
    }

    const auto action_id = action.get("ActionID");

    auto pending = std::make_shared<PendingResponse>();
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_[action_id] = pending;
    }

    try {
        tcp_client_.write(action.to_wire_format());
    } catch (...) {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_.erase(action_id);
        throw;
    }

    std::unique_lock<std::mutex> lock(pending->mutex);
    const bool completed = pending->cv.wait_for(lock, timeout, [&pending] { return pending->ready; });
    if (!completed) {
        std::lock_guard<std::mutex> pending_lock(pending_mutex_);
        pending_.erase(action_id);
        throw std::runtime_error("Timeout while waiting for AMI response");
    }

    {
        std::lock_guard<std::mutex> pending_lock(pending_mutex_);
        pending_.erase(action_id);
    }

    return pending->message;
}

std::string AmiClient::next_action_id() {
    return std::string("amicpp-") + std::to_string(next_action_id_.fetch_add(1));
}

void AmiClient::reader_loop() {
    while (running_.load()) {
        try {
            const auto frame = tcp_client_.read_frame();
            auto message = parse_ami_message(frame);

            if (message.empty()) {
                continue;
            }

            route_message(message);
        } catch (...) {
            running_.store(false);
            break;
        }
    }
}

void AmiClient::route_message(const AmiMessage& message) {
    if (message.has("Event")) {
        std::unordered_map<std::string, EventHandler> handlers_copy;
        {
            std::lock_guard<std::mutex> lock(handlers_mutex_);
            handlers_copy = handlers_;
        }

        for (const auto& handler_entry : handlers_copy) {
            try {
                handler_entry.second(message);
            } catch (...) {
            }
        }
        return;
    }

    if (message.has("Response") && message.has("ActionID")) {
        std::shared_ptr<PendingResponse> pending;
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            auto it = pending_.find(message.get("ActionID"));
            if (it != pending_.end()) {
                pending = it->second;
            }
        }

        if (pending) {
            {
                std::lock_guard<std::mutex> lock(pending->mutex);
                pending->message = message;
                pending->ready = true;
            }
            pending->cv.notify_all();
        }
    }
}

} // namespace amicpp
