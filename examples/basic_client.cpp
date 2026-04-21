// Copyright (c) 2026 Daniele Pallastrelli
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE or copy at
//  http://www.boost.org/LICENSE_1_0.txt)

#include <amicpp/ami_client.hpp>
#include <amicpp/ami_session.hpp>

#include <boost/asio.hpp>

#include <iostream>
#include <string>
#include <thread>

namespace {

void print_usage(const char* program) {
    std::cout << "Usage: " << program
              << " [host] [port] [username] [secret]\n"
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

    const std::string host = argc > 1 ? argv[1] : "127.0.0.1";
    const std::string port = argc > 2 ? argv[2] : "5038";
    const std::string username = argc > 3 ? argv[3] : "admin";
    const std::string secret = argc > 4 ? argv[4] : "admin";

    std::cout << "Connecting to " << host << ":" << port
              << " as " << username << "...\n";

    boost::asio::io_context io_context;
    auto work_guard = boost::asio::make_work_guard(io_context);
    std::thread io_thread([&io_context] {
        io_context.run();
    });

    try {
        amicpp::AmiClient client(io_context);

        client.async_connect(host, port, [&client, &username, &secret](const std::string& result) {
            if (result.find("error") != std::string::npos || result.find("Error") != std::string::npos) {
                std::cerr << "Connection failed: " << result << std::endl;
                return;
            }

            std::cout << "Connected to AMI: " << result << std::endl;

            auto handler_id = client.add_event_handler([](const amicpp::AmiMessage& event) {
                if (event.has("Event")) {
                    std::cout << "[EVENT] " << event.get("Event") << std::endl;
                }
            });

            {
                amicpp::AmiSession session(client, username, secret, "on");

                amicpp::AmiMessage action;
                action.set("Action", "Ping");

                client.async_send_action(std::move(action), [&client, handler_id](bool success, const amicpp::AmiMessage& response) {
                    if (success) {
                        std::cout << "Ping response: " << response.get("Response", "Unknown")
                                  << " - " << response.get("Message", "") << std::endl;
                    } else {
                        std::cout << "Ping failed\n";
                    }
                    
                    client.remove_event_handler(handler_id);
                    client.async_disconnect();
                });
            }
        });

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
