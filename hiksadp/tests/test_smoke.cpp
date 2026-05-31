#include "core/csv.hpp"
#include "management/device_manager.hpp"
#include "management/password_reset_service.hpp"

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

hiksadp::Device make_device_with_model(const std::string& mac, const std::string& ip, const std::string& model)
{
    auto d = make_device(mac, ip);
    d.model = model;
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

    DeviceManager export_mgr;
    auto weird = make_device_with_model("11:22:33:44:55:66", "10.0.0.9", "Cam,\"Lab\"");
    export_mgr.update_devices({weird});

    auto csv = export_mgr.export_csv();
    check(csv.has_value(), "export csv ok");
    if (csv.has_value()) {
        const auto escaped_pos = csv->find("\"Cam,\"\"Lab\"\"\"");
        if (escaped_pos == std::string::npos) {
            std::cerr << "CSV output:\n" << *csv << "\n";
        }
        check(escaped_pos != std::string::npos, "export csv escapes quote+comma field");
    }

    auto valid_dev = make_device("22:33:44:55:66:77", "10.0.0.10");
    mgr.update_devices({valid_dev});

    auto weak_pw = mgr.apply_password_reset_questions(
        valid_dev.mac_address, "a1", "a2", "a3", Password{"123"});
    check(!weak_pw.has_value() && weak_pw.error().code == ErrorCode::WeakPassword,
          "security question reset rejects weak password");

    auto empty_answer = mgr.apply_password_reset_questions(
        valid_dev.mac_address, "", "a2", "a3", Password{"Admin123!"});
    check(!empty_answer.has_value() && empty_answer.error().code == ErrorCode::EmptyInput,
          "security question reset rejects empty answer");

    PasswordResetService prs;
    const std::string xml =
        "<PasswordResetResponse><DeviceSN>SN-1</DeviceSN><Date>20260531</Date>"
        "<SecurityCode>ABC123</SecurityCode></PasswordResetResponse>";
    auto parsed = prs.parse_response_xml(xml);
    check(parsed.has_value(), "password reset xml parse ok");
    if (parsed.has_value()) {
        check(parsed->serial == "SN-1", "password reset parse serial");
        check(parsed->timestamp == "20260531", "password reset parse timestamp");
        check(parsed->reset_code == "ABC123", "password reset parse security code");
    }

    if (g_failed > 0) {
        std::cerr << "Smoke tests failed: " << g_failed << "\n";
        return 1;
    }
    std::cout << "Smoke tests passed\n";
    return 0;
}
