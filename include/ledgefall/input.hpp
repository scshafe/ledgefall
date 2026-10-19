#pragma once
// One player's input for one tick, as the core sees it: the app's input module
// turns a controller or the keyboard into this, and the netcode carries it as
// two bytes. Dead zones and rebinding are the app's; the core sees only a
// quantised axis and two buttons.

#include <cstdint>
#include <span>

namespace ledgefall {

enum Button : std::uint8_t {
  kButtonJump = 1 << 0,  // held; a press is the tick it becomes held
  kButtonDown = 1 << 1,  // held: fast-fall in the air, drop through a platform you stand on
};
constexpr std::uint8_t kButtonMask = kButtonJump | kButtonDown;

constexpr int kAxisMax = 100;  // move_x is -100..100, 100 being full tilt

struct Input {
  std::int8_t move_x = 0;
  std::uint8_t buttons = 0;

  friend bool operator==(const Input&, const Input&) = default;
};

constexpr std::size_t kInputBytes = 2;

void encode(const Input& in, std::uint8_t out[kInputBytes]) noexcept;
// False, leaving `out` alone, if the bytes are not a valid input (wrong size,
// axis out of range, unknown button bits).
bool decode(std::span<const std::uint8_t> bytes, Input& out) noexcept;

}  // namespace ledgefall
