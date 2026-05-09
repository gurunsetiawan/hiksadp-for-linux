#pragma once

// Tidak ada Qt include di sini — QObject disembunyikan di .cpp via pimpl
// Ini menghindari masalah Qt 6.4 moc yang tidak support C++23 concepts

#include "sadp_packet.hpp"
#include "core/device.hpp"
#include "core/result.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hiksadp::protocol {

// ── NetworkInterface ───────────────────────────────────────────────────────
struct NetworkInterface {
    std::string name;
    IpAddress   address;
    IpAddress   broadcast;
    bool        is_up{false};
};

[[nodiscard]] std::vector<NetworkInterface> get_active_interfaces();

// ── DiscoveryResult ────────────────────────────────────────────────────────
struct DiscoveryResult {
    std::vector<Device>                          devices;
    std::chrono::steady_clock::time_point        scan_time;
    std::string                                  interface_used;
    int                                          packets_sent{0};
    int                                          responses_received{0};
    int                                          datagrams_received{0};
    int                                          datagrams_parse_failed{0};
};

// ── SadpDiscovery — public API (QObject disembunyikan di pimpl) ────────────
class SadpDiscovery {
public:
    static constexpr int DEFAULT_TIMEOUT_MS = 4500;
    static constexpr int AUTO_REFRESH_MS    = 15000;

    explicit SadpDiscovery();
    ~SadpDiscovery();

    SadpDiscovery(const SadpDiscovery&)            = delete;
    SadpDiscovery& operator=(const SadpDiscovery&) = delete;

    // Konfigurasi
    void set_interface(const NetworkInterface& iface);
    void set_timeout(int timeout_ms);
    void set_auto_refresh(bool enabled);

    // Callbacks
    using DeviceFoundCallback  = std::function<void(const Device&)>;
    using ScanCompleteCallback = std::function<void(const DiscoveryResult&)>;
    using ErrorCallback        = std::function<void(const AppError&)>;

    void on_device_found(DeviceFoundCallback cb);
    void on_scan_complete(ScanCompleteCallback cb);
    void on_error(ErrorCallback cb);

    // Control
    [[nodiscard]] Result<void> start_scan();
    void stop();

    [[nodiscard]] bool                is_scanning() const noexcept;
    [[nodiscard]] std::vector<Device> last_discovered() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace hiksadp::protocol
