#include "core/csv.hpp"
#include "management/device_manager.hpp"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {
int g_failed = 0;

void check(bool cond, const std::string& msg)
{
    if (!cond) {
        ++g_failed;
        std::cerr << "[FAIL] " << msg << "\n";
    } else {
        std::cout << "[OK] " << msg << "\n";
    }
}

hiksadp::Device make_device(const std::string& mac, const std::string& ip)
{
    using namespace hiksadp;
    Device d{
        .serial_number = SerialNumber{"SN-TEST-001"},
        .mac_address = MacAddress{mac},
        .firmware_version = FirmwareVersion{"V1"},
        .model = "DS-TEST",
        .device_type = "IPCamera",
        .password_reset_mode = "",
        .support_reset = "",
        .network = NetworkConfig{
            .ip = IpAddress{ip},
            .subnet_mask = IpAddress{"255.255.255.0"},
            .gateway = IpAddress{"192.168.1.1"},
            .http_port = Port{80},
            .sdk_port = Port{8000},
            .dhcp_enabled = false,
        },
        .state = StateActive{},
        .last_seen = std::chrono::steady_clock::now(),
    };
    return d;
}
} // namespace

int main()
{
    using namespace hiksadp;
    using namespace std::chrono_literals;

    check(escape_csv_field("abc123") == "abc123", "csv plain");
    check(escape_csv_field("a,b") == "\"a,b\"", "csv comma");
    check(escape_csv_field("a\"b") == "\"a\"\"b\"", "csv quote escaped");
    check(escape_csv_field("a\nb") == "\"a\nb\"", "csv newline quoted");

    DeviceManager mgr;
    mgr.set_retention_policy(std::chrono::seconds{1}, std::chrono::seconds{1});
    auto [stale_after, purge_after] = mgr.retention_policy();
    check(stale_after.count() >= 5, "retention stale clamped min 5s");
    check(purge_after.count() > stale_after.count(), "retention purge > stale");

    mgr.set_retention_policy(std::chrono::seconds{5}, std::chrono::seconds{10});
    auto dev = make_device("AA:BB:CC:DD:EE:FF", "192.168.1.64");
    mgr.update_devices({dev});
    check(mgr.devices().size() == 1, "device added");

    std::this_thread::sleep_for(6s);
    mgr.update_devices({});
    auto stale = mgr.find_by_mac(MacAddress{"AA:BB:CC:DD:EE:FF"});
    check(stale.has_value(), "device still present before purge");
    check(stale.has_value() && is_error(stale->state), "device marked stale as error");

    std::this_thread::sleep_for(5s);
    mgr.update_devices({});
    auto purged = mgr.find_by_mac(MacAddress{"AA:BB:CC:DD:EE:FF"});
    check(!purged.has_value(), "device purged after purge ttl");

    if (g_failed > 0) {
        std::cerr << "Smoke tests failed: " << g_failed << "\n";
        return 1;
    }
    std::cout << "Smoke tests passed\n";
    return 0;
}
