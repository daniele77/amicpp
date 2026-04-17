// Copyright (c) 2026 Daniele Pallastrelli
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE or copy at
//  http://www.boost.org/LICENSE_1_0.txt)

#include <amicpp/ami_message.hpp>

#include <sstream>

namespace amicpp {

void AmiMessage::add(std::string key, std::string value) {
    fields_.emplace_back(std::move(key), std::move(value));
}

void AmiMessage::set(std::string key, std::string value) {
    for (auto& field : fields_) {
        if (field.first == key) {
            field.second = std::move(value);
            return;
        }
    }

    add(std::move(key), std::move(value));
}

bool AmiMessage::has(const std::string& key) const {
    for (auto it = fields_.rbegin(); it != fields_.rend(); ++it) {
        if (it->first == key) {
            return true;
        }
    }

    return false;
}

std::string AmiMessage::get(const std::string& key, const std::string& fallback) const {
    for (auto it = fields_.rbegin(); it != fields_.rend(); ++it) {
        if (it->first == key) {
            return it->second;
        }
    }

    return fallback;
}

const AmiMessage::FieldList& AmiMessage::fields() const noexcept {
    return fields_;
}

bool AmiMessage::empty() const noexcept {
    return fields_.empty();
}

std::string AmiMessage::to_wire_format() const {
    std::ostringstream oss;

    for (const auto& field : fields_) {
        oss << field.first << ": " << field.second << "\r\n";
    }

    oss << "\r\n";
    return oss.str();
}

AmiMessage parse_ami_message(const std::string& payload) {
    AmiMessage message;

    std::istringstream input(payload);
    std::string line;

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) {
            continue;
        }

        const auto separator = line.find(':');
        if (separator == std::string::npos) {
            continue;
        }

        auto key = line.substr(0, separator);
        auto value = line.substr(separator + 1);
        if (!value.empty() && value.front() == ' ') {
            value.erase(value.begin());
        }

        message.add(std::move(key), std::move(value));
    }

    return message;
}

} // namespace amicpp
