// Copyright (c) 2026 Daniele Pallastrelli
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE or copy at
//  http://www.boost.org/LICENSE_1_0.txt)

#include <amicpp/tcp_client.hpp>

namespace amicpp {

TcpClient::TcpClient(boost::asio::io_context& io_context)
    : io_context_(io_context),
      resolver_(io_context_),
      socket_(io_context_),
      connected_(false) {}

TcpClient::~TcpClient() {
    close_socket();
}

void TcpClient::async_connect(const std::string& host, const std::string& port, ConnectHandler handler) {
    resolver_.async_resolve(
        host,
        port,
        [this, handler](const boost::system::error_code& resolve_error,
                        const boost::asio::ip::tcp::resolver::results_type& endpoints) {
            if (resolve_error) {
                connected_.store(false);
                handler(resolve_error);
                return;
            }

            boost::asio::async_connect(
                socket_,
                endpoints,
                [this, handler](const boost::system::error_code& connect_error,
                                const boost::asio::ip::tcp::endpoint&) {
                    connected_.store(!connect_error);
                    handler(connect_error);
                });
        });
}

void TcpClient::async_disconnect() {
    boost::asio::post(io_context_, [this] {
        close_socket();
    });
}

bool TcpClient::is_connected() const {
    return connected_.load();
}

void TcpClient::async_write(std::string payload, WriteHandler handler) {
    boost::asio::post(io_context_, [this, payload, handler]() mutable {
        if (!socket_.is_open()) {
            connected_.store(false);
            handler(make_error_code(boost::system::errc::not_connected));
            return;
        }

        const bool write_in_progress = !pending_writes_.empty();
        pending_writes_.push_back(PendingWrite{std::move(payload), std::move(handler)});
        if (!write_in_progress) {
            start_next_write();
        }
    });
}

void TcpClient::async_read_line(LineHandler handler) {
    boost::asio::async_read_until(
        socket_,
        read_buffer_,
        "\r\n",
        [this, handler](const boost::system::error_code& error, std::size_t bytes) {
            if (error) {
                connected_.store(false);
                handler(error, std::string());
                return;
            }

            auto line = consume_from_buffer(bytes);
            if (line.size() >= 2 && line.compare(line.size() - 2, 2, "\r\n") == 0) {
                line.erase(line.size() - 2);
            }

            handler(error, std::move(line));
        });
}

void TcpClient::async_read_frame(FrameHandler handler) {
    boost::asio::async_read_until(
        socket_,
        read_buffer_,
        "\r\n\r\n",
        [this, handler](const boost::system::error_code& error, std::size_t bytes) {
            if (error) {
                connected_.store(false);
                handler(error, std::string());
                return;
            }

            handler(error, consume_from_buffer(bytes));
        });
}

std::string TcpClient::consume_from_buffer(std::size_t size) {
    std::string output(size, '\0');

    std::istream input(&read_buffer_);
    input.read(&output[0], static_cast<std::streamsize>(size));

    return output;
}

void TcpClient::start_next_write() {
    if (pending_writes_.empty()) {
        return;
    }

    boost::asio::async_write(
        socket_,
        boost::asio::buffer(pending_writes_.front().payload),
        [this](const boost::system::error_code& error, std::size_t) {
            auto pending = std::move(pending_writes_.front());
            pending_writes_.pop_front();

            if (error) {
                connected_.store(false);
            }

            pending.handler(error);

            if (!error) {
                start_next_write();
            } else {
                pending_writes_.clear();
            }
        });
}

void TcpClient::close_socket() {
    pending_writes_.clear();
    if (!socket_.is_open()) {
        connected_.store(false);
        return;
    }

    resolver_.cancel();

    boost::system::error_code ec;
    socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    socket_.cancel(ec);
    socket_.close(ec);
    connected_.store(false);
}

} // namespace amicpp
