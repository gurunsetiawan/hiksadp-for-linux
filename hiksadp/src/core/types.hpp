#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace hiksadp {

// ── Strong typedef helper ─────────────────────────────────────────────────
//
// Membungkus tipe primitif dalam struct agar tidak bisa saling ditukar.
// IpAddress dan MacAddress sama-sama berisi string, tapi tidak bisa
// diassign satu sama lain — compiler langsung error.
//
template <typename T, typename Tag>
struct StrongType {
    T value{};

    constexpr explicit StrongType() = default;
    constexpr explicit StrongType(T v) : value{std::move(v)} {}

    [[nodiscard]] constexpr const T& get() const noexcept { return value; }
    [[nodiscard]] constexpr T&       get()       noexcept { return value; }

    constexpr auto operator<=>(const StrongType&) const = default;
    constexpr bool operator==(const StrongType&) const  = default;
};

// ── Domain primitives ─────────────────────────────────────────────────────

// IP address dalam bentuk string "192.168.1.100"
struct IpAddressTag {};
using IpAddress = StrongType<std::string, IpAddressTag>;

// Port number 1-65535
struct PortTag {};
using Port = StrongType<std::uint16_t, PortTag>;

// MAC address dalam bentuk string "AA:BB:CC:DD:EE:FF"
struct MacAddressTag {};
using MacAddress = StrongType<std::string, MacAddressTag>;

// Serial number perangkat (unik per unit)
struct SerialNumberTag {};
using SerialNumber = StrongType<std::string, SerialNumberTag>;

// Versi firmware, mis. "V5.7.3_build230615"
struct FirmwareVersionTag {};
using FirmwareVersion = StrongType<std::string, FirmwareVersionTag>;

// Password device (tidak pernah di-log atau ditampilkan)
struct PasswordTag {};
using Password = StrongType<std::string, PasswordTag>;

// ── Port constants ─────────────────────────────────────────────────────────
namespace ports {
    constexpr Port SADP_DISCOVERY{37020};  // UDP broadcast discovery
    constexpr Port SDK_DEFAULT{8000};       // Hikvision SDK port default
    constexpr Port HTTP_DEFAULT{80};
    constexpr Port HTTPS_DEFAULT{443};
}

// ── Validation helpers ─────────────────────────────────────────────────────

// Validasi format IP address (IPv4 only untuk v1.0)
[[nodiscard]] bool is_valid_ip(std::string_view ip) noexcept;

// Validasi format MAC address (XX:XX:XX:XX:XX:XX atau XX-XX-XX-XX-XX-XX)
[[nodiscard]] bool is_valid_mac(std::string_view mac) noexcept;

// Validasi kekuatan password sesuai requirement Hikvision:
// minimal 8 karakter, kombinasi ≥3 dari: huruf besar, huruf kecil, angka, simbol
[[nodiscard]] bool is_strong_password(std::string_view password) noexcept;

// Validasi port number
[[nodiscard]] constexpr bool is_valid_port(std::uint16_t port) noexcept {
    return port >= 1; // port 0 reserved
}

// ── Formatting helpers ─────────────────────────────────────────────────────

[[nodiscard]] inline std::string format_ip(const IpAddress& ip) {
    return ip.get();
}

[[nodiscard]] inline std::string format_mac(const MacAddress& mac) {
    return mac.get();
}

} // namespace hiksadp
