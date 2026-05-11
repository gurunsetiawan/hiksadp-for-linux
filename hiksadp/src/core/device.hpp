#pragma once

#include "types.hpp"
#include <chrono>
#include <string>
#include <variant>

namespace hiksadp {

// ── Device status sebagai variant (bukan enum + flag boolean) ─────────────
//
// std::variant memastikan compiler paksa kita handle SEMUA state.
// Tidak ada kemungkinan state yang "tidak mungkin" seperti
// { active=true, inactive=true } yang bisa terjadi dengan bool flags.

struct StateInactive {
    // Perangkat baru terhubung, belum ada password admin yang di-set
};

struct StateActive {
    // Perangkat sudah diaktivasi dan bisa dikonfigurasi
};

struct StateError {
    std::string reason; // penjelasan kenapa error
};

using DeviceState = std::variant<StateInactive, StateActive, StateError>;

// Helper untuk cek state dengan cara yang readable
[[nodiscard]] constexpr bool is_inactive(const DeviceState& s) noexcept {
    return std::holds_alternative<StateInactive>(s);
}
[[nodiscard]] constexpr bool is_active(const DeviceState& s) noexcept {
    return std::holds_alternative<StateActive>(s);
}
[[nodiscard]] constexpr bool is_error(const DeviceState& s) noexcept {
    return std::holds_alternative<StateError>(s);
}

// State sebagai string untuk display
[[nodiscard]] inline std::string state_to_string(const DeviceState& s) {
    return std::visit([](auto&& state) -> std::string {
        using T = std::decay_t<decltype(state)>;
        if constexpr (std::is_same_v<T, StateInactive>) return "Inactive";
        if constexpr (std::is_same_v<T, StateActive>)   return "Active";
        if constexpr (std::is_same_v<T, StateError>)    return "Error: " + state.reason;
    }, s);
}

// ── NetworkConfig — konfigurasi jaringan perangkat ────────────────────────
struct NetworkConfig {
    IpAddress ip;
    IpAddress subnet_mask;
    IpAddress gateway;
    Port      http_port{ports::HTTP_DEFAULT};
    Port      sdk_port{ports::SDK_DEFAULT};
    bool      dhcp_enabled{false};
};

// ── Device — representasi satu perangkat Hikvision di jaringan ────────────
//
// Immutable setelah konstruksi kecuali via DeviceManager.
// Semua field harus terisi — tidak ada optional yang tersembunyi.
//
struct Device {
    // Identitas (dari UDP discovery response)
    SerialNumber    serial_number;
    MacAddress      mac_address;
    FirmwareVersion firmware_version;
    std::string     model;           // mis. "DS-2CD2143G2-I"
    std::string     device_type;     // mis. "IPCamera", "NVR", "DVR"
    std::string     password_reset_mode; // dari SADP tag PasswordResetModeSecond
    std::string     support_reset;       // dari SADP tag Support

    // Network
    NetworkConfig   network;

    // State
    DeviceState     state;

    // Metadata
    std::chrono::steady_clock::time_point last_seen;

    // Display helpers
    [[nodiscard]] std::string display_name() const {
        return model + " (" + serial_number.get() + ")";
    }

    [[nodiscard]] std::string status_string() const {
        return state_to_string(state);
    }
};

} // namespace hiksadp
