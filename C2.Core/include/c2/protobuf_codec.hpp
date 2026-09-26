#pragma once

#include "c2/protocol.hpp"

#include <cstddef>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace c2::protobuf {
enum class DecodeError { malformed, missing_payload, invalid_message };
using DecodeResult = std::variant<Envelope, DecodeError>;

[[nodiscard]] std::vector<std::byte> encode(const Envelope& envelope);
[[nodiscard]] DecodeResult decode(std::span<const std::byte> bytes);
[[nodiscard]] std::string_view to_string(DecodeError error) noexcept;
}  // namespace c2::protobuf
