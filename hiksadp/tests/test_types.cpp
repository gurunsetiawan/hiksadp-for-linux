#include <catch2/catch_test_macros.hpp>
#include "core/types.hpp"
#include "core/result.hpp"
#include "core/device.hpp"

using namespace hiksadp;

// ── Strong typedef tests ───────────────────────────────────────────────────

TEST_CASE("StrongType tidak bisa saling ditukar", "[types]") {
    IpAddress  ip{  "192.168.1.1" };
    MacAddress mac{ "AA:BB:CC:DD:EE:FF" };

    // Ini harus compile error jika uncomment:
    // ip = mac;  // ERROR: tidak bisa assign MacAddress ke IpAddress

    REQUIRE(ip.get()  == "192.168.1.1");
    REQUIRE(mac.get() == "AA:BB:CC:DD:EE:FF");
}

TEST_CASE("StrongType equality dan comparison bekerja", "[types]") {
    IpAddress a{"192.168.1.1"};
    IpAddress b{"192.168.1.1"};
    IpAddress c{"192.168.1.2"};

    REQUIRE(a == b);
    REQUIRE(a != c);
    REQUIRE(c > a);
}

// ── IP validation ──────────────────────────────────────────────────────────

TEST_CASE("is_valid_ip menerima IP yang valid", "[validation]") {
    REQUIRE(is_valid_ip("192.168.1.1"));
    REQUIRE(is_valid_ip("10.0.0.1"));
    REQUIRE(is_valid_ip("255.255.255.0"));
    REQUIRE(is_valid_ip("0.0.0.0"));
    REQUIRE(is_valid_ip("172.16.0.1"));
    REQUIRE(is_valid_ip("1.2.3.4"));
}

TEST_CASE("is_valid_ip menolak IP yang tidak valid", "[validation]") {
    REQUIRE_FALSE(is_valid_ip("256.0.0.1"));
    REQUIRE_FALSE(is_valid_ip("192.168.1"));
    REQUIRE_FALSE(is_valid_ip("192.168.1.1.1"));
    REQUIRE_FALSE(is_valid_ip("abc.def.ghi.jkl"));
    REQUIRE_FALSE(is_valid_ip(""));
    REQUIRE_FALSE(is_valid_ip("192.168.1."));
    REQUIRE_FALSE(is_valid_ip(" 192.168.1.1"));
}

// ── MAC validation ─────────────────────────────────────────────────────────

TEST_CASE("is_valid_mac menerima MAC yang valid", "[validation]") {
    REQUIRE(is_valid_mac("AA:BB:CC:DD:EE:FF"));
    REQUIRE(is_valid_mac("aa:bb:cc:dd:ee:ff"));
    REQUIRE(is_valid_mac("00:00:00:00:00:00"));
    REQUIRE(is_valid_mac("FF:FF:FF:FF:FF:FF"));
    REQUIRE(is_valid_mac("AA-BB-CC-DD-EE-FF")); // format dash
    REQUIRE(is_valid_mac("12:34:56:78:9A:BC"));
}

TEST_CASE("is_valid_mac menolak MAC yang tidak valid", "[validation]") {
    REQUIRE_FALSE(is_valid_mac("AA:BB:CC:DD:EE"));
    REQUIRE_FALSE(is_valid_mac("AA:BB:CC:DD:EE:FF:00"));
    REQUIRE_FALSE(is_valid_mac("GG:BB:CC:DD:EE:FF"));
    REQUIRE_FALSE(is_valid_mac(""));
    REQUIRE_FALSE(is_valid_mac("AA BB CC DD EE FF"));
    REQUIRE_FALSE(is_valid_mac("AABBCCDDEEFF"));
}

// ── Password validation ────────────────────────────────────────────────────

TEST_CASE("is_strong_password menerima password kuat", "[validation]") {
    // Huruf besar + huruf kecil + angka (3 kategori)
    REQUIRE(is_strong_password("Hikvision1"));
    // Huruf besar + huruf kecil + angka + simbol
    REQUIRE(is_strong_password("Hikvision1!"));
    REQUIRE(is_strong_password("Admin@1234"));
    REQUIRE(is_strong_password("Camera#01"));
    // Huruf kecil + angka + simbol
    REQUIRE(is_strong_password("camera1!ok"));
}

TEST_CASE("is_strong_password menolak password lemah", "[validation]") {
    // Terlalu pendek
    REQUIRE_FALSE(is_strong_password("Ab1!"));
    // Hanya huruf kecil
    REQUIRE_FALSE(is_strong_password("abcdefgh"));
    // Hanya angka
    REQUIRE_FALSE(is_strong_password("12345678"));
    // Hanya 2 kategori (huruf kecil + angka)
    REQUIRE_FALSE(is_strong_password("camera123"));
    // Kosong
    REQUIRE_FALSE(is_strong_password(""));
    // Terlalu panjang (>16)
    REQUIRE_FALSE(is_strong_password("Hikvision1!TooLong"));
}

// ── DeviceState variant ────────────────────────────────────────────────────

TEST_CASE("DeviceState variant berfungsi dengan benar", "[device]") {
    SECTION("StateInactive") {
        DeviceState state = StateInactive{};
        REQUIRE(is_inactive(state));
        REQUIRE_FALSE(is_active(state));
        REQUIRE_FALSE(is_error(state));
        REQUIRE(state_to_string(state) == "Inactive");
    }

    SECTION("StateActive") {
        DeviceState state = StateActive{};
        REQUIRE(is_active(state));
        REQUIRE_FALSE(is_inactive(state));
        REQUIRE_FALSE(is_error(state));
        REQUIRE(state_to_string(state) == "Active");
    }

    SECTION("StateError") {
        DeviceState state = StateError{"koneksi terputus"};
        REQUIRE(is_error(state));
        REQUIRE_FALSE(is_active(state));
        REQUIRE_FALSE(is_inactive(state));
        REQUIRE(state_to_string(state) == "Error: koneksi terputus");
    }
}

// ── Result<T> / std::expected ──────────────────────────────────────────────

TEST_CASE("Result<T> success path bekerja", "[result]") {
    auto result = make_ok(IpAddress{"192.168.1.1"});
    REQUIRE(result.has_value());
    REQUIRE(result.value().get() == "192.168.1.1");
}

TEST_CASE("Result<T> error path bekerja", "[result]") {
    auto result = make_error<IpAddress>(ErrorCode::InvalidIpAddress, "999.0.0.1");
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == ErrorCode::InvalidIpAddress);
    REQUIRE(result.error().detail == "999.0.0.1");
    REQUIRE_FALSE(result.error().message().empty());
}

TEST_CASE("AppError message tidak kosong untuk semua ErrorCode", "[result]") {
    // Pastikan semua ErrorCode punya pesan yang meaningful
    const std::vector<ErrorCode> all_codes = {
        ErrorCode::SocketCreateFailed,
        ErrorCode::SocketBindFailed,
        ErrorCode::BroadcastFailed,
        ErrorCode::ReceiveTimeout,
        ErrorCode::NetworkUnreachable,
        ErrorCode::InvalidPacket,
        ErrorCode::InvalidChecksum,
        ErrorCode::UnexpectedResponse,
        ErrorCode::XmlParseFailed,
        ErrorCode::DeviceNotFound,
        ErrorCode::DeviceAlreadyActive,
        ErrorCode::DeviceInactive,
        ErrorCode::AuthenticationFailed,
        ErrorCode::OperationTimeout,
        ErrorCode::InvalidIpAddress,
        ErrorCode::InvalidPort,
        ErrorCode::InvalidMacAddress,
        ErrorCode::WeakPassword,
        ErrorCode::EmptyInput,
        ErrorCode::InternalError,
    };

    for (auto code : all_codes) {
        AppError err{code};
        REQUIRE_FALSE(err.message().empty());
    }
}
