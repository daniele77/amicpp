// Copyright (c) 2026 Daniele Pallastrelli
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE or copy at
//  http://www.boost.org/LICENSE_1_0.txt)

#include <amicpp/ami_client.hpp>

#include <boost/asio.hpp>

#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace {

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [host] [port] [username] [secret]\n"
              << "\n"
              << "Defaults:\n"
              << "  host     : 127.0.0.1\n"
              << "  port     : 5038\n"
              << "  username : admin\n"
              << "  secret   : admin\n";
}

bool is_connect_error(const std::string& result) {
    return result.find("Connect error:") == 0 || result.find("Banner read error:") == 0;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc == 2 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help")) {
        print_usage(argv[0]);
        return 0;
    }

    const std::string host = argc > 1 ? argv[1] : "127.0.0.1";
    const std::string port = argc > 2 ? argv[2] : "5038";
    const std::string username = argc > 3 ? argv[3] : "admin";
    const std::string secret = argc > 4 ? argv[4] : "admin";

    boost::asio::io_context io_context;
    auto work_guard = boost::asio::make_work_guard(io_context);
    std::thread io_thread([&io_context] { io_context.run(); });

    std::mutex done_mutex;
    std::condition_variable done_cv;
    bool done = false;

    auto finish = [&](int exit_code) {
        {
            std::lock_guard<std::mutex> lock(done_mutex);
            done = true;
        }
        done_cv.notify_one();
        return exit_code;
    };

    int exit_code = 0;

    amicpp::AmiClient client(io_context);

    client.async_connect(host, port, [&](const std::string& result) {
        if (is_connect_error(result)) {
            std::cerr << "Connect failed: " << result << std::endl;
            exit_code = 1;
            finish(exit_code);
            return;
        }

        std::cout << "Connected: " << result << std::endl;

        auto event_handler_id = client.add_event_handler([](const amicpp::AmiMessage& event) {
            if (event.has("Event")) {
                std::cout << "[EVENT] " << event.get("Event") << std::endl;
            }
        });

        amicpp::AmiMessage login;
        login.set("Action", "Login");
        login.set("Username", username);
        login.set("Secret", secret);
        login.set("Events", "on");

        client.async_send_action(std::move(login), [&](bool login_ok, const amicpp::AmiMessage& login_response) {
            if (!login_ok || login_response.get("Response") != "Success") {
                std::cerr << "Login failed: " << login_response.get("Message", "unknown error") << std::endl;
                client.remove_event_handler(event_handler_id);
                client.async_disconnect();
                exit_code = 1;
                finish(exit_code);
                return;
            }

            amicpp::AmiMessage ping;
            ping.set("Action", "Ping");

            client.async_send_action(std::move(ping), [&](bool ping_ok, const amicpp::AmiMessage& ping_response) {
                if (ping_ok) {
                    std::cout << "Ping response: " << ping_response.get("Response", "Unknown")
                              << " - " << ping_response.get("Message", "") << std::endl;
                } else {
                    std::cerr << "Ping failed: " << ping_response.get("Message", "unknown error") << std::endl;
                    exit_code = 1;
                }

                amicpp::AmiMessage logoff;
                logoff.set("Action", "Logoff");

                client.async_send_action(std::move(logoff), [&](bool, const amicpp::AmiMessage&) {
                    client.remove_event_handler(event_handler_id);
                    client.async_disconnect();
                    finish(exit_code);
                });
            });
        });
    });

    {
        std::unique_lock<std::mutex> lock(done_mutex);
        done_cv.wait(lock, [&done] { return done; });
    }

    work_guard.reset();
    io_context.stop();
    if (io_thread.joinable()) {
        io_thread.join();
    }

    return exit_code;
}
