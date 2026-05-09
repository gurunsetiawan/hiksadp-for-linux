#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <type_traits>

namespace hiksadp {

// ── AppError ──────────────────────────────────────────────────────────────
//
// Satu-satunya error type di seluruh aplikasi.
// Menggunakan enum class agar compiler paksa kita handle semua kasus.
//
enum class ErrorCode {
    // Network
    SocketCreateFailed,
    SocketBindFailed,
    BroadcastFailed,
    ReceiveTimeout,
    NetworkUnreachable,

    // Protocol
    InvalidPacket,
    InvalidChecksum,
    UnexpectedResponse,
    XmlParseFailed,

    // Device
    DeviceNotFound,
    DeviceAlreadyActive,
    DeviceInactive,
    AuthenticationFailed,
    OperationTimeout,

    // Input validation
    InvalidIpAddress,
    InvalidPort,
    InvalidMacAddress,
    WeakPassword,
    EmptyInput,

    // System
    InternalError,
};

// Konversi ErrorCode ke string yang human-readable
constexpr std::string_view to_string(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::SocketCreateFailed:    return "Gagal membuat socket jaringan";
        case ErrorCode::SocketBindFailed:      return "Gagal bind socket ke interface";
        case ErrorCode::BroadcastFailed:       return "Gagal mengirim broadcast packet";
        case ErrorCode::ReceiveTimeout:        return "Timeout menunggu respons perangkat";
        case ErrorCode::NetworkUnreachable:    return "Jaringan tidak dapat dijangkau";
        case ErrorCode::InvalidPacket:         return "Format packet tidak valid";
        case ErrorCode::InvalidChecksum:       return "Checksum packet tidak cocok";
        case ErrorCode::UnexpectedResponse:    return "Respons tidak terduga dari perangkat";
        case ErrorCode::XmlParseFailed:        return "Gagal mem-parse XML respons";
        case ErrorCode::DeviceNotFound:        return "Perangkat tidak ditemukan di jaringan";
        case ErrorCode::DeviceAlreadyActive:   return "Perangkat sudah dalam status aktif";
        case ErrorCode::DeviceInactive:        return "Perangkat belum diaktivasi";
        case ErrorCode::AuthenticationFailed:  return "Autentikasi gagal, periksa password";
        case ErrorCode::OperationTimeout:      return "Operasi timeout";
        case ErrorCode::InvalidIpAddress:      return "Format IP address tidak valid";
        case ErrorCode::InvalidPort:           return "Nomor port tidak valid (1-65535)";
        case ErrorCode::InvalidMacAddress:     return "Format MAC address tidak valid";
        case ErrorCode::WeakPassword:          return "Password terlalu lemah (min 8 karakter, huruf besar/kecil/angka/simbol)";
        case ErrorCode::EmptyInput:            return "Input tidak boleh kosong";
        case ErrorCode::InternalError:         return "Terjadi kesalahan internal";
    }
    return "Unknown error";
}

struct AppError {
    ErrorCode   code;
    std::string detail; // opsional: konteks tambahan (mis. IP address yang gagal)

    explicit AppError(ErrorCode c, std::string d = {})
        : code{c}, detail{std::move(d)} {}

    // Pesan lengkap untuk ditampilkan ke user
    [[nodiscard]] std::string message() const {
        auto base = std::string{to_string(code)};
        if (!detail.empty()) {
            base += " — ";
            base += detail;
        }
        return base;
    }
};

// ── Result<T> ─────────────────────────────────────────────────────────────
//
// Semua fungsi yang bisa gagal HARUS mengembalikan Result<T>.
// Gunakan [[nodiscard]] di definisi fungsi agar caller tidak bisa abaikan error.
//
// Contoh:
//   [[nodiscard]] Result<DeviceList> scan_network(NetworkInterface iface);
//
//   auto result = scan_network(iface);
//   if (!result) {
//       show_error(result.error().message());
//       return;
//   }
//   auto& devices = result.value();
//
template <typename T>
using Result = std::expected<T, AppError>;

// Helper: buat error result dengan sintaks lebih pendek
template <typename T = void>
[[nodiscard]] auto make_error(ErrorCode code, std::string detail = {}) {
    return Result<T>{std::unexpected(AppError{code, std::move(detail)})};
}

// Helper: buat success result
template <typename T>
[[nodiscard]] auto make_ok(T&& value) {
    using ValueType = std::remove_cvref_t<T>;
    return Result<ValueType>{std::forward<T>(value)};
}

} // namespace hiksadp
