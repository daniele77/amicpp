// Copyright (c) 2026 Daniele Pallastrelli
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE or copy at
//  http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <boost/asio.hpp>

#include <atomic>
#include <deque>

#include <string>

namespace amicpp {

class TcpClient {
public:
    using ConnectHandler = std::function<void(const boost::system::error_code&)>;
    using WriteHandler = std::function<void(const boost::system::error_code&)>;
    using LineHandler = std::function<void(const boost::system::error_code&, std::string)>;
    using FrameHandler = std::function<void(const boost::system::error_code&, std::string)>;

    explicit TcpClient(boost::asio::io_context& io_context);
    ~TcpClient();

    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;

    void async_connect(const std::string& host, const std::string& port, ConnectHandler handler);
    void async_disconnect();

    bool is_connected() const;

    void async_write(std::string payload, WriteHandler handler);
    void async_read_line(LineHandler handler);
    void async_read_frame(FrameHandler handler);

private:
    struct PendingWrite {
        std::string payload;
        WriteHandler handler;
    };

    std::string consume_from_buffer(std::size_t size);
    void start_next_write();
    void close_socket();

    boost::asio::io_context& io_context_;
    boost::asio::ip::tcp::resolver resolver_;
    boost::asio::ip::tcp::socket socket_;
    boost::asio::streambuf read_buffer_;
    std::deque<PendingWrite> pending_writes_;
    std::atomic<bool> connected_;
};

} // namespace amicpp
