// Copyright (c) 2026 Daniele Pallastrelli
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE or copy at
//  http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace amicpp {

class AmiMessage {
public:
    using Field = std::pair<std::string, std::string>;
    using FieldList = std::vector<Field>;

    AmiMessage() = default;

    void add(std::string key, std::string value);
    void set(std::string key, std::string value);

    bool has(const std::string& key) const;
    std::string get(const std::string& key, const std::string& fallback = "") const;

    const FieldList& fields() const noexcept;
    bool empty() const noexcept;

    std::string to_wire_format() const;

private:
    FieldList fields_;
};

AmiMessage parse_ami_message(const std::string& payload);

} // namespace amicpp
