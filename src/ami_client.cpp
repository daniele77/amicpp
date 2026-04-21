// Copyright (c) 2026 Daniele Pallastrelli
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE or copy at
//  http://www.boost.org/LICENSE_1_0.txt)

#include <amicpp/ami_client.hpp>

#include <condition_variable>
#include <stdexcept>
#include <thread>
#include <vector>

namespace amicpp {

AmiClient::AmiClient(boost::asio::io_context& io_context)
    : io_context_(io_context),
      tcp_client_(io_context_),
      connected_(false),
      next_action_id_(1) {}

AmiClient::~AmiClient() {
    async_disconnect();
}

void AmiClient::connect(const std::string& host, const std::string& port) {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    std::string result;

    async_connect(host, port, [&mutex, &cv, &done, &result](const std::string& connect_result) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            done = true;
            result = connect_result;
        }
        cv.notify_one();
    });

    while (true) {
        std::unique_lock<std::mutex> lock(mutex);
        if (done) {
            break;
        }
        lock.unlock();

        io_context_.restart();
        if (io_context_.run_one() == 0) {
            std::this_thread::yield();
        }
    }

    if (is_error_connect_result(result)) {
        throw std::runtime_error("AMI connect failed: " + result);
    }
}

void AmiClient::disconnect() {
    async_disconnect();
}

void AmiClient::async_connect(const std::string& host, const std::string& port, ConnectHandler handler) {
    if (is_connected()) {
        boost::asio::post(io_context_, [handler, banner = banner_]() {
            handler(banner.empty() ? "Already connected" : banner);
        });
        return;
    }

    tcp_client_.async_connect(host, port, [this, handler](const boost::system::error_code& error) {
        if (error) {
            connected_.store(false);
            boost::asio::post(io_context_, [handler, error]() {
                handler(std::string("Connect error: ") + error.message());
            });
            return;
        }

        tcp_client_.async_read_line([this, handler](const boost::system::error_code& banner_error, std::string banner_line) {
            if (banner_error) {
                connected_.store(false);
                tcp_client_.async_disconnect();
                boost::asio::post(io_context_, [handler, banner_error]() {
                    handler(std::string("Banner read error: ") + banner_error.message());
                });
                return;
            }

            banner_ = std::move(banner_line);
            connected_.store(true);
            start_read_loop();

            boost::asio::post(io_context_, [handler, banner = banner_]() {
                handler(banner);
            });
        });
    });
}

void AmiClient::async_disconnect() {
    const bool was_connected = connected_.exchange(false);
    if (was_connected) {
        fail_all_pending("Disconnected");
    }

    boost::asio::post(io_context_, [this]() {
        tcp_client_.async_disconnect();
    });
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
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    bool success = false;
    AmiMessage response;

    async_send_action(
        std::move(action),
        [&mutex, &cv, &done, &success, &response](bool ok, const AmiMessage& message) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                done = true;
                success = ok;
                response = message;
            }
            cv.notify_one();
        },
        timeout);

    while (true) {
        std::unique_lock<std::mutex> lock(mutex);
        if (done) {
            break;
        }
        lock.unlock();

        io_context_.restart();
        if (io_context_.run_one() == 0) {
            std::this_thread::yield();
        }
    }

    if (!success) {
        throw std::runtime_error(response.get("Message", "AMI action failed"));
    }

    return response;
}

void AmiClient::async_send_action(AmiMessage action, ActionHandler handler) {
    async_send_action(std::move(action), std::move(handler), std::chrono::milliseconds(5000));
}

void AmiClient::async_send_action(
    AmiMessage action,
    ActionHandler handler,
    std::chrono::milliseconds timeout) {
    if (!is_connected()) {
        boost::asio::post(io_context_, [handler]() {
            AmiMessage response;
            response.set("Response", "Error");
            response.set("Message", "AMI client is not connected");
            handler(false, response);
        });
        return;
    }

    if (!action.has("Action")) {
        boost::asio::post(io_context_, [handler]() {
            AmiMessage response;
            response.set("Response", "Error");
            response.set("Message", "AMI action must include the 'Action' field");
            handler(false, response);
        });
        return;
    }

    if (!action.has("ActionID")) {
        action.set("ActionID", next_action_id());
    }

    const auto action_id = action.get("ActionID");
    auto pending = std::make_shared<PendingAction>();
    pending->handler = std::move(handler);
    pending->timer = std::make_shared<boost::asio::steady_timer>(io_context_);

    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_[action_id] = pending;
    }

    pending->timer->expires_after(timeout);
    pending->timer->async_wait([this, action_id, pending](const boost::system::error_code& error) {
        if (error == boost::asio::error::operation_aborted) {
            return;
        }

        if (pending->completed.exchange(true)) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            pending_.erase(action_id);
        }

        auto handler_copy = pending->handler;
        auto timeout_response = make_error_response("Timeout while waiting for AMI response");
        boost::asio::post(io_context_, [handler_copy, timeout_response]() {
            handler_copy(false, timeout_response);
        });
    });

    const auto wire = action.to_wire_format();
    boost::asio::post(io_context_, [this, action_id, pending, wire]() {
        tcp_client_.async_write(wire, [this, action_id, pending](const boost::system::error_code& error) {
            if (error) {
                if (pending->completed.exchange(true)) {
                    return;
                }

                pending->timer->cancel();
                auto handler_copy = pending->handler;
                {
                    std::lock_guard<std::mutex> lock(pending_mutex_);
                    pending_.erase(action_id);
                }
                auto write_error = make_error_response("AMI write failed: " + error.message());
                boost::asio::post(io_context_, [handler_copy, write_error]() {
                    handler_copy(false, write_error);
                });
            }
        });
    });
}

std::string AmiClient::next_action_id() {
    return std::string("amicpp-") + std::to_string(next_action_id_.fetch_add(1));
}

bool AmiClient::is_error_connect_result(const std::string& result) const {
    return result.find("Connect error:") == 0 || result.find("Banner read error:") == 0;
}

AmiMessage AmiClient::make_error_response(const std::string& message) const {
    AmiMessage response;
    response.set("Response", "Error");
    response.set("Message", message);
    return response;
}

void AmiClient::start_read_loop() {
    if (!connected_.load()) {
        return;
    }

    tcp_client_.async_read_frame([this](const boost::system::error_code& error, std::string frame) {
        if (error) {
            connected_.store(false);
            fail_all_pending("Read error");
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
    std::vector<std::shared_ptr<PendingAction>> pending_actions;

    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        for (auto& entry : pending_) {
            pending_actions.push_back(entry.second);
        }
        pending_.clear();
    }

    for (const auto& pending : pending_actions) {
        if (pending->completed.exchange(true)) {
            continue;
        }

        pending->timer->cancel();
        auto handler_copy = pending->handler;
        auto error_response = make_error_response(reason);
        boost::asio::post(io_context_, [handler_copy, error_response]() {
            handler_copy(false, error_response);
        });
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
        std::shared_ptr<PendingAction> pending;
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            auto it = pending_.find(message.get("ActionID"));
            if (it != pending_.end()) {
                pending = it->second;
            }
        }

        if (pending) {
            if (pending->completed.exchange(true)) {
                return;
            }

            pending->timer->cancel();
            auto handler_copy = pending->handler;
            auto response_copy = message;
            const auto action_id = message.get("ActionID");
            {
                std::lock_guard<std::mutex> lock(pending_mutex_);
                pending_.erase(action_id);
            }
            boost::asio::post(io_context_, [handler_copy, response_copy]() {
                handler_copy(true, response_copy);
            });
        }
    }
}

} // namespace amicpp
