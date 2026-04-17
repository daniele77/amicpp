// Copyright (c) 2026 Daniele Pallastrelli
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE or copy at
//  http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <boost/asio.hpp>

#include <mutex>
#include <string>

namespace amicpp {

class TcpClient {
public:
    TcpClient();
    ~TcpClient();

    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;

    void connect(const std::string& host, const std::string& port);
    void disconnect();

    bool is_connected() const;

    void write(const std::string& payload);
    std::string read_line();
    std::string read_frame();

private:
    boost::asio::io_context io_context_;
    boost::asio::ip::tcp::socket socket_;
    boost::asio::streambuf read_buffer_;
    mutable std::mutex socket_mutex_;
};

} // namespace amicpp
