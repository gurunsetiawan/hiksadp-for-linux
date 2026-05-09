#pragma once

#include "core/types.hpp"
#include "core/result.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace hiksadp::protocol {

// ── SADP Packet Constants ──────────────────────────────────────────────────
//
// Diperoleh dari reverse-engineering protokol SADP oleh komunitas open source
// (referensi: MatrixEditor/hiktools, MIT License)
//
inline constexpr std::uint16_t SADP_SEQUENCE        = 0x0000;
inline constexpr std::size_t   SADP_HEADER_SIZE     = 20;
inline constexpr std::uint8_t  SADP_VERSION         = 0x02;

// Command types
enum class SadpCommand : std::uint8_t {
    Inquiry       = 0x01,  // broadcast: "siapa saja yang ada di jaringan?"
    InquiryReply  = 0x02,  // reply dari perangkat
    Activate      = 0x03,  // aktivasi perangkat inactive
    ActivateReply = 0x04,
    ModifyNet     = 0x05,  // ubah konfigurasi jaringan
    ModifyReply   = 0x06,
    ResetPassword = 0x07,
    ResetReply    = 0x08,
};

// ── Parsed device info dari UDP response ──────────────────────────────────
//
// Semua field di sini adalah strong types, bukan raw string/int.
// Tidak ada ambiguitas tentang apa arti setiap field.
//
struct SadpDeviceInfo {
    MacAddress      mac_address;
    IpAddress       ip_address;
    IpAddress       subnet_mask;
    IpAddress       gateway;
    Port            sdk_port;
    Port            http_port;
    SerialNumber    serial_number;
    FirmwareVersion firmware_version;
    std::string     model;
    std::string     device_type;
    bool            dhcp_enabled{false};
    bool            is_inactive{false};
};

// ── PacketBuilder — membangun UDP packet untuk dikirim ─────────────────────
//
// Semua method mengembalikan Result<> — tidak ada silent failure.
// [[nodiscard]] memastikan caller tidak bisa abaikan return value.
//
class PacketBuilder {
public:
    PacketBuilder() = default;

    // Bangun inquiry broadcast packet
    [[nodiscard]] static Result<std::vector<std::byte>>
    build_inquiry(std::uint16_t sequence_number = 0);

    // Bangun activate packet
    [[nodiscard]] static Result<std::vector<std::byte>>
    build_activate(const MacAddress& target_mac, const Password& new_password);

    // Bangun modify network packet
    [[nodiscard]] static Result<std::vector<std::byte>>
    build_modify_network(
        const MacAddress&    target_mac,
        const Password&      password,
        const IpAddress&     new_ip,
        const IpAddress&     new_mask,
        const IpAddress&     new_gateway,
        Port                 new_sdk_port,
        Port                 new_http_port,
        bool                 dhcp_enabled
    );

    // Hitung checksum SADP — algoritma dari reverse-engineering komunitas
    // Public agar bisa dipakai oleh PacketParser dan tests
    [[nodiscard]] static std::uint8_t
    calculate_checksum(std::span<const std::byte> data) noexcept;
};

// ── PacketParser — parse UDP response dari perangkat ──────────────────────
class PacketParser {
public:
    // Parse raw bytes jadi SadpDeviceInfo
    // Mengembalikan error jika format tidak valid atau checksum salah
    [[nodiscard]] static Result<SadpDeviceInfo>
    parse_inquiry_reply(std::span<const std::byte> data);

    // Parse reply dari activate command
    [[nodiscard]] static Result<bool>
    parse_activate_reply(std::span<const std::byte> data);

    // Parse reply dari modify network command
    [[nodiscard]] static Result<bool>
    parse_modify_reply(std::span<const std::byte> data);

    // Debug helper: ringkasan field packet untuk diagnosis parser.
    [[nodiscard]] static std::string
    debug_summary(std::span<const std::byte> data);

private:
    [[nodiscard]] static bool
    verify_checksum(std::span<const std::byte> data) noexcept;

    [[nodiscard]] static Result<std::string>
    extract_xml_payload(std::span<const std::byte> data);
};

} // namespace hiksadp::protocol
