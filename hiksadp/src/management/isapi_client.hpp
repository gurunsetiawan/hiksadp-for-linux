#pragma once

#include "core/device.hpp"
#include "core/result.hpp"
#include "core/types.hpp"

#include <memory>
#include <string>

namespace hiksadp {

// ── IsapiCredential — autentikasi ke device ───────────────────────────────
struct IsapiCredential {
    IpAddress ip;
    Port      http_port{ports::HTTP_DEFAULT};
    Password  password;
    std::string username{"admin"}; // default Hikvision
};

// ── IsapiResponse — raw response dari device ──────────────────────────────
struct IsapiResponse {
    int         status_code{0};
    std::string body;

    [[nodiscard]] bool is_ok() const noexcept {
        return status_code >= 200 && status_code < 300;
    }
};

// ── ActivationRequest ─────────────────────────────────────────────────────
struct ActivationRequest {
    IpAddress ip;
    Port      http_port{ports::HTTP_DEFAULT};
    Password  new_password;
};

// ── NetworkConfigRequest ──────────────────────────────────────────────────
struct NetworkConfigRequest {
    IsapiCredential credential;
    IpAddress       new_ip;
    IpAddress       new_mask;
    IpAddress       new_gateway;
    Port            new_http_port{ports::HTTP_DEFAULT};
    Port            new_sdk_port{ports::SDK_DEFAULT};
    bool            dhcp_enabled{false};
};

// ── IsapiClient — semua komunikasi ke device via HTTP ─────────────────────
//
// Setiap method mengembalikan Result<T> — caller wajib handle error.
// Semua operasi blocking (untuk async, wrap dalam QThread / std::async).
//
// QNetworkAccessManager disembunyikan di pimpl agar header bersih dari Qt.
//
class IsapiClient {
public:
    static constexpr int DEFAULT_TIMEOUT_MS = 5000;

    explicit IsapiClient();
    ~IsapiClient();

    IsapiClient(const IsapiClient&)            = delete;
    IsapiClient& operator=(const IsapiClient&) = delete;

    void set_timeout(int ms);

    // ── Aktivasi device baru (Inactive → Active) ──────────────────────────
    // Endpoint: PUT /ISAPI/Security/userCheck
    [[nodiscard]] Result<void>
    activate_device(const ActivationRequest& req);

    // ── Ambil info device (verifikasi koneksi) ────────────────────────────
    // Endpoint: GET /ISAPI/System/deviceInfo
    [[nodiscard]] Result<std::string>
    get_device_info(const IsapiCredential& cred);

    // ── Ubah konfigurasi jaringan ─────────────────────────────────────────
    // Endpoint: PUT /ISAPI/System/Network/interfaces/1
    [[nodiscard]] Result<void>
    set_network_config(const NetworkConfigRequest& req);

    // ── Reboot device ─────────────────────────────────────────────────────
    // Endpoint: PUT /ISAPI/System/reboot
    [[nodiscard]] Result<void>
    reboot_device(const IsapiCredential& cred);

    // ── Cek apakah device aktif (bisa untuk polling status) ───────────────
    [[nodiscard]] Result<bool>
    is_device_active(const IpAddress& ip, Port http_port);

private:
    [[nodiscard]] Result<IsapiResponse>
    http_get(const IsapiCredential& cred, const std::string& path);

    [[nodiscard]] Result<IsapiResponse>
    http_put(const IsapiCredential& cred,
             const std::string& path,
             const std::string& xml_body);

    // PUT tanpa auth (untuk aktivasi — device belum punya password)
    [[nodiscard]] Result<IsapiResponse>
    http_put_noauth(const IpAddress& ip, Port port,
                    const std::string& path,
                    const std::string& xml_body);

    [[nodiscard]] std::string build_base_url(const IpAddress& ip,
                                              Port port) const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace hiksadp
