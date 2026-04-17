// Copyright (c) 2026 Daniele Pallastrelli
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE or copy at
//  http://www.boost.org/LICENSE_1_0.txt)

#include <amicpp/tcp_client.hpp>

#include <stdexcept>

namespace amicpp {

namespace {

std::string consume_from_buffer(boost::asio::streambuf& buffer, std::size_t size) {
    std::string output(size, '\0');

    std::istream input(&buffer);
    input.read(&output[0], static_cast<std::streamsize>(size));

    return output;
}

} // namespace

TcpClient::TcpClient()
    : socket_(io_context_) {}

TcpClient::~TcpClient() {
    disconnect();
}

void TcpClient::connect(const std::string& host, const std::string& port) {
    std::lock_guard<std::mutex> lock(socket_mutex_);

    if (socket_.is_open()) {
        return;
    }

    boost::asio::ip::tcp::resolver resolver(io_context_);
    auto endpoints = resolver.resolve(host, port);
    boost::asio::connect(socket_, endpoints);
}

void TcpClient::disconnect() {
    std::lock_guard<std::mutex> lock(socket_mutex_);

    if (!socket_.is_open()) {
        return;
    }

    boost::system::error_code ec;
    socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    socket_.close(ec);
}

bool TcpClient::is_connected() const {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    return socket_.is_open();
}

void TcpClient::write(const std::string& payload) {
    std::lock_guard<std::mutex> lock(socket_mutex_);

    if (!socket_.is_open()) {
        throw std::runtime_error("TCP socket is not connected");
    }

    boost::asio::write(socket_, boost::asio::buffer(payload));
}

std::string TcpClient::read_line() {
    std::lock_guard<std::mutex> lock(socket_mutex_);

    if (!socket_.is_open()) {
        throw std::runtime_error("TCP socket is not connected");
    }

    const auto bytes = boost::asio::read_until(socket_, read_buffer_, "\r\n");
    auto line = consume_from_buffer(read_buffer_, bytes);

    if (line.size() >= 2 && line.compare(line.size() - 2, 2, "\r\n") == 0) {
        line.erase(line.size() - 2);
    }

    return line;
}

std::string TcpClient::read_frame() {
    std::lock_guard<std::mutex> lock(socket_mutex_);

    if (!socket_.is_open()) {
        throw std::runtime_error("TCP socket is not connected");
    }

    const auto bytes = boost::asio::read_until(socket_, read_buffer_, "\r\n\r\n");
    return consume_from_buffer(read_buffer_, bytes);
}

} // namespace amicpp
