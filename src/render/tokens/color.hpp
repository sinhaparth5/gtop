#pragma once
//
// Token layer — colour primitive.
//
// The only colour *type*. Everything downstream (palette, semantic roles,
// gradients) is expressed in terms of Rgb, so there is exactly one
// representation of colour in the codebase.
//
#include <cstdint>

namespace gtop::render::tokens {

struct Rgb {
    std::uint8_t r{};
    std::uint8_t g{};
    std::uint8_t b{};

    friend constexpr bool operator==(Rgb, Rgb) = default;
};

// consteval, not constexpr: a hex literal may only ever be turned into a colour
// at compile time. This is what makes the "no raw hex outside palette.hpp" rule
// mechanically meaningful rather than a convention people drift away from.
consteval Rgb hex(std::uint32_t v) noexcept {
    return Rgb{static_cast<std::uint8_t>((v >> 16) & 0xFF),
               static_cast<std::uint8_t>((v >> 8) & 0xFF),
               static_cast<std::uint8_t>(v & 0xFF)};
}

}  // namespace gtop::render::tokens
