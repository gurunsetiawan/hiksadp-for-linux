#pragma once

#include "core/device.hpp"
#include "core/result.hpp"
#include "management/isapi_client.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <chrono>

namespace hiksadp {

// ── BatchResult — hasil operasi batch ─────────────────────────────────────
struct BatchItemResult {
    MacAddress  mac;
    std::string device_label; // "Model (IP)" untuk display
    bool        success{false};
    std::string error_message;
};

struct BatchResult {
    std::vector<BatchItemResult> items;

    [[nodiscard]] int success_count() const {
        int n = 0;
        for (const auto& i : items) if (i.success) ++n;
        return n;
    }
    [[nodiscard]] int failure_count() const {
        return static_cast<int>(items.size()) - success_count();
    }
    [[nodiscard]] bool all_success() const {
        return failure_count() == 0;
    }
};

// ── IpSequenceConfig — konfigurasi untuk batch IP assignment ──────────────
struct IpSequenceConfig {
    IpAddress start_ip;      // mis. "192.168.1.64"
    IpAddress subnet_mask;
    IpAddress gateway;
    Port      http_port{ports::HTTP_DEFAULT};
    Port      sdk_port{ports::SDK_DEFAULT};
    bool      dhcp_enabled{false};
};

// ── DeviceManager — orchestrator semua operasi device ─────────────────────
//
// Satu-satunya titik masuk untuk operasi device dari UI layer.
// UI tidak boleh langsung mengakses IsapiClient atau SadpDiscovery.
//
class DeviceManager {
public:
    explicit DeviceManager();
    ~DeviceManager();

    DeviceManager(const DeviceManager&)            = delete;
    DeviceManager& operator=(const DeviceManager&) = delete;

    // ── Callbacks untuk UI ────────────────────────────────────────────────
    using DeviceListChangedCallback = std::function<void(const std::vector<Device>&)>;
    using ProgressCallback          = std::function<void(int current, int total, const std::string& label)>;
    void on_device_list_changed(DeviceListChangedCallback cb);
    void on_progress(ProgressCallback cb);

    // ── Device list management ────────────────────────────────────────────

    // Update list device (dipanggil oleh scanner)
    void update_devices(const std::vector<Device>& devices);

    // Ambil snapshot current device list
    [[nodiscard]] std::vector<Device> devices() const;

    // Cari device berdasarkan MAC
    [[nodiscard]] std::optional<Device>
    find_by_mac(const MacAddress& mac) const;

    // Retention policy untuk hasil discovery
    void set_retention_policy(std::chrono::seconds stale_after,
                              std::chrono::seconds purge_after);
    [[nodiscard]] std::pair<std::chrono::seconds, std::chrono::seconds>
    retention_policy() const;

    // ── Aktivasi ──────────────────────────────────────────────────────────

    [[nodiscard]] Result<void>
    activate_device(const MacAddress& mac, const Password& password);

    [[nodiscard]] BatchResult
    activate_batch(const std::vector<MacAddress>& macs, const Password& password);

    // ── Network config ────────────────────────────────────────────────────

    [[nodiscard]] Result<void>
    set_network_config(const MacAddress& mac,
                       const Password& password,
                       const IpAddress& new_ip,
                       const IpAddress& new_mask,
                       const IpAddress& new_gateway,
                       Port new_http_port,
                       Port new_sdk_port,
                       bool dhcp_enabled);

    // Batch: assign IP berurutan mulai dari start_ip
    [[nodiscard]] BatchResult
    assign_sequential_ips(const std::vector<MacAddress>& macs,
                           const Password& password,
                           const IpSequenceConfig& config);

    // Preview IP yang akan di-assign sebelum dikonfirmasi
    [[nodiscard]] std::vector<std::pair<MacAddress, IpAddress>>
    preview_sequential_ips(const std::vector<MacAddress>& macs,
                            const IpSequenceConfig& config) const;

    // ── Reboot ────────────────────────────────────────────────────────────

    [[nodiscard]] Result<void>
    reboot_device(const MacAddress& mac, const Password& password);

    [[nodiscard]] Result<void>
    change_admin_password(const MacAddress& mac,
                          const Password& old_password,
                          const Password& new_password);

    [[nodiscard]] BatchResult
    reboot_batch(const std::vector<MacAddress>& macs, const Password& password);

    // ── Password reset via SADP security code ────────────────────────────
    [[nodiscard]] Result<void>
    apply_password_reset_code(const MacAddress& mac,
                              const std::string& reset_code,
                              const Password& new_password);

    [[nodiscard]] Result<void>
    apply_password_reset_questions(const MacAddress& mac,
                                   const std::string& answer1,
                                   const std::string& answer2,
                                   const std::string& answer3,
                                   const Password& new_password);

    // ── Export ────────────────────────────────────────────────────────────

    [[nodiscard]] Result<std::string> export_csv() const;
    [[nodiscard]] Result<std::string> export_xml() const;

private:
    [[nodiscard]] static IpAddress
    increment_ip(const IpAddress& ip, int offset);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace hiksadp
