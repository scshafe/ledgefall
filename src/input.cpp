#include "ledgefall/input.hpp"

namespace ledgefall {

void encode(const Input& in, std::uint8_t out[kInputBytes]) noexcept {
  out[0] = static_cast<std::uint8_t>(in.move_x);
  out[1] = in.buttons;
}

bool decode(std::span<const std::uint8_t> bytes, Input& out) noexcept {
  if (bytes.size() != kInputBytes) return false;
  const auto axis = static_cast<std::int8_t>(bytes[0]);
  if (axis < -kAxisMax || axis > kAxisMax || (bytes[1] & ~kButtonMask) != 0) return false;
  out.move_x = axis;
  out.buttons = bytes[1];
  return true;
}

}  // namespace ledgefall
