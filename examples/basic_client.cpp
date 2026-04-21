// Copyright (c) 2026 Daniele Pallastrelli
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE or copy at
//  http://www.boost.org/LICENSE_1_0.txt)

#include <amicpp/ami_client.hpp>
#include <amicpp/ami_session.hpp>

#include <boost/asio.hpp>

#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace {

void print_usage(const char* program) {
    std::cout << "Usage: " << program
              << " [--sync|--async] [host] [port] [username] [secret]\n"
              << "\n"
              << "Modes:\n"
              << "  --async : non-blocking API (default)\n"
              << "  --sync  : blocking API wrappers\n"
              << "\n"
              << "Defaults:\n"
              << "  host     : 127.0.0.1\n"
              << "  port     : 5038\n"
              << "  username : admin\n"
              << "  secret   : admin\n";
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc == 2 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help")) {
        print_usage(argv[0]);
        return 0;
    }

    int arg_index = 1;
    bool sync_mode = false;
    if (argc > arg_index && std::string(argv[arg_index]) == "--sync") {
        sync_mode = true;
        ++arg_index;
    } else if (argc > arg_index && std::string(argv[arg_index]) == "--async") {
        ++arg_index;
    }

    const std::string host = argc > arg_index ? argv[arg_index] : "127.0.0.1";
    const std::string port = argc > arg_index + 1 ? argv[arg_index + 1] : "5038";
    const std::string username = argc > arg_index + 2 ? argv[arg_index + 2] : "admin";
    const std::string secret = argc > arg_index + 3 ? argv[arg_index + 3] : "admin";

    std::cout << "Connecting to " << host << ":" << port
              << " as " << username << (sync_mode ? " [sync]" : " [async]") << "...\n";

    boost::asio::io_context io_context;
    auto work_guard = boost::asio::make_work_guard(io_context);
    std::thread io_thread([&io_context] {
        io_context.run();
    });

    try {
        amicpp::AmiClient client(io_context);

        if (sync_mode) {
            client.connect(host, port);
            std::cout << "Connected to AMI: " << client.banner() << std::endl;

            auto handler_id = client.add_event_handler([](const amicpp::AmiMessage& event) {
                if (event.has("Event")) {
                    std::cout << "[EVENT] " << event.get("Event") << std::endl;
                }
            });

            {
                amicpp::AmiSession session(client, username, secret, "on");

                amicpp::AmiMessage action;
                action.set("Action", "Ping");

                const auto response = client.send_action(std::move(action));
                std::cout << "Ping response: " << response.get("Response", "Unknown")
                          << " - " << response.get("Message", "") << std::endl;
            }

            client.remove_event_handler(handler_id);
            client.disconnect();
        } else {
            std::mutex done_mutex;
            std::condition_variable done_cv;
            bool done = false;

            client.async_connect(host, port, [&client, &username, &secret, &done_mutex, &done_cv, &done](const std::string& result) {
                if (result.find("Connect error:") == 0 || result.find("Banner read error:") == 0) {
                    std::cerr << "Connection failed: " << result << std::endl;
                    {
                        std::lock_guard<std::mutex> lock(done_mutex);
                        done = true;
                    }
                    done_cv.notify_one();
                    return;
                }

                std::cout << "Connected to AMI: " << result << std::endl;

                auto handler_id = client.add_event_handler([](const amicpp::AmiMessage& event) {
                    if (event.has("Event")) {
                        std::cout << "[EVENT] " << event.get("Event") << std::endl;
                    }
                });

                amicpp::AmiMessage login;
                login.set("Action", "Login");
                login.set("Username", username);
                login.set("Secret", secret);
                login.set("Events", "on");

                client.async_send_action(std::move(login), [&client, handler_id, &done_mutex, &done_cv, &done](bool login_success, const amicpp::AmiMessage& login_response) {
                    if (!login_success || login_response.get("Response") != "Success") {
                        std::cout << "Login failed: " << login_response.get("Message", "unknown error") << std::endl;
                        client.remove_event_handler(handler_id);
                        client.async_disconnect();
                        {
                            std::lock_guard<std::mutex> lock(done_mutex);
                            done = true;
                        }
                        done_cv.notify_one();
                        return;
                    }

                    amicpp::AmiMessage ping;
                    ping.set("Action", "Ping");

                    client.async_send_action(std::move(ping), [&client, handler_id, &done_mutex, &done_cv, &done](bool success, const amicpp::AmiMessage& response) {
                        if (success) {
                            std::cout << "Ping response: " << response.get("Response", "Unknown")
                                      << " - " << response.get("Message", "") << std::endl;
                        } else {
                            std::cout << "Ping failed: " << response.get("Message", "unknown error") << std::endl;
                        }

                        amicpp::AmiMessage logoff;
                        logoff.set("Action", "Logoff");
                        client.async_send_action(std::move(logoff), [&client, handler_id, &done_mutex, &done_cv, &done](bool, const amicpp::AmiMessage&) {
                            client.remove_event_handler(handler_id);
                            client.async_disconnect();

                            {
                                std::lock_guard<std::mutex> lock(done_mutex);
                                done = true;
                            }
                            done_cv.notify_one();
                        });
                    });
                });
            });

            std::unique_lock<std::mutex> lock(done_mutex);
            done_cv.wait(lock, [&done] { return done; });
        }

        work_guard.reset();
        io_context.stop();

        io_thread.join();

        return 0;
    } catch (const std::exception& ex) {
        work_guard.reset();
        io_context.stop();
        if (io_thread.joinable()) {
            io_thread.join();
        }

        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }
}
