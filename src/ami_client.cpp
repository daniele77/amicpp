// Copyright (c) 2026 Daniele Pallastrelli
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE or copy at
//  http://www.boost.org/LICENSE_1_0.txt)

#include <amicpp/ami_client.hpp>

#include <stdexcept>

namespace amicpp {

AmiClient::AmiClient(boost::asio::io_context& io_context)
    : io_context_(io_context),
      tcp_client_(io_context_),
      connected_(false),
      next_action_id_(1) {}

AmiClient::~AmiClient() {
    disconnect();
}

void AmiClient::connect(const std::string& host, const std::string& port) {
    if (is_connected()) {
        return;
    }

    std::mutex connect_mutex;
    std::condition_variable connect_cv;
    bool connect_done = false;
    std::string connect_error;

    boost::asio::post(io_context_, [this, host, port, &connect_mutex, &connect_cv, &connect_done, &connect_error] {
        tcp_client_.async_connect(host, port, [this, &connect_mutex, &connect_cv, &connect_done, &connect_error](const boost::system::error_code& error) {
            if (error) {
                connect_error = error.message();
                {
                    std::lock_guard<std::mutex> lock(connect_mutex);
                    connect_done = true;
                }
                connect_cv.notify_one();
                return;
            }

            tcp_client_.async_read_line([this, &connect_mutex, &connect_cv, &connect_done, &connect_error](
                                            const boost::system::error_code& banner_error,
                                            std::string banner_line) {
                if (banner_error) {
                    connect_error = banner_error.message();
                    connected_.store(false);
                    tcp_client_.async_disconnect();
                } else {
                    banner_ = std::move(banner_line);
                    connected_.store(true);
                    start_read_loop();
                }

                {
                    std::lock_guard<std::mutex> lock(connect_mutex);
                    connect_done = true;
                }
                connect_cv.notify_one();
            });
        });
    });

    std::unique_lock<std::mutex> lock(connect_mutex);
    connect_cv.wait(lock, [&connect_done] { return connect_done; });

    if (!connect_error.empty()) {
        throw std::runtime_error("AMI connect failed: " + connect_error);
    }
}

void AmiClient::disconnect() {
    const bool was_connected = connected_.exchange(false);

    boost::asio::post(io_context_, [this] {
        tcp_client_.async_disconnect();
    });

    if (was_connected) {
        fail_all_pending("AMI client disconnected");
    }
}

bool AmiClient::is_connected() const {
    return connected_.load();
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

    const auto wire = action.to_wire_format();
    boost::asio::post(io_context_, [this, action_id, pending, wire] {
        tcp_client_.async_write(wire, [this, action_id, pending](const boost::system::error_code& error) {
            if (!error) {
                return;
            }

            {
                std::lock_guard<std::mutex> lock(pending->mutex);
                pending->failed = true;
                pending->error_message = "AMI write failed: " + error.message();
                pending->ready = true;
            }
            pending->cv.notify_all();

            std::lock_guard<std::mutex> pending_lock(pending_mutex_);
            pending_.erase(action_id);
        });
    });

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

    if (pending->failed) {
        throw std::runtime_error(pending->error_message);
    }

    return pending->message;
}

std::string AmiClient::next_action_id() {
    return std::string("amicpp-") + std::to_string(next_action_id_.fetch_add(1));
}

void AmiClient::start_read_loop() {
    if (!connected_.load()) {
        return;
    }

    tcp_client_.async_read_frame([this](const boost::system::error_code& error, std::string frame) {
        if (error) {
            connected_.store(false);
            fail_all_pending("AMI read failed: " + error.message());
            return;
        }

        auto message = parse_ami_message(frame);
        if (!message.empty()) {
            route_message(message);
        }

        start_read_loop();
    });
}

void AmiClient::fail_all_pending(const std::string& reason) {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    for (auto& entry : pending_) {
        auto& pending = entry.second;
        {
            std::lock_guard<std::mutex> pending_lock(pending->mutex);
            pending->failed = true;
            pending->error_message = reason;
            pending->ready = true;
        }
        pending->cv.notify_all();
    }
    pending_.clear();
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
