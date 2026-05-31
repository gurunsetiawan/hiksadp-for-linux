#include "protocol/sadp_discovery.hpp"
#include "core/device.hpp"
#include "core/logger.hpp"

#include <QCoreApplication>
#include <QTimer>
#include <QDir>
#include <QStandardPaths>

#include <format>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

using namespace hiksadp;
using namespace hiksadp::protocol;

// ── Terminal formatting helpers ────────────────────────────────────────────

static void print_separator(char ch = '-', int width = 100) {
    std::cout << std::string(static_cast<std::size_t>(width), ch) << "\n";
}

static void print_header() {
    print_separator('=');
    std::cout << "  HikSADP Linux — Device Scanner v1.0\n";
    std::cout << "  Hikvision SADP Tool alternative for Linux\n";
    print_separator('=');
    std::cout << "\n";
}

static void print_device_table_header() {
    print_separator();
    std::cout << std::left
              << std::setw(18) << "IP Address"
              << std::setw(20) << "MAC Address"
              << std::setw(36) << "Serial Number"
              << std::setw(20) << "Model"
              << std::setw(12) << "Status"
              << std::setw(10) << "HTTP Port"
              << "Firmware\n";
    print_separator();
}

static std::string sanitize_serial_for_display(const Device& d) {
    auto serial = d.serial_number.get();
    if (serial.empty()) return serial;

    serial.erase(
        std::remove_if(serial.begin(), serial.end(), [](unsigned char ch) {
            return std::iscntrl(ch) != 0;
        }),
        serial.end());

    if (!d.model.empty() &&
        serial.size() > d.model.size() &&
        serial.ends_with(d.model)) {
        serial.erase(serial.size() - d.model.size());
    }

    const auto trim_at_second_prefix = [&](std::string_view token) {
        const auto first = serial.find(token);
        if (first == std::string::npos) return;
        const auto second = serial.find(token, first + token.size());
        if (second != std::string::npos && second > 0) {
            serial.erase(second);
        }
    };

    trim_at_second_prefix("DS-");
    trim_at_second_prefix("CS-");

    // Fallback deterministik: potong saat kemunculan prefix model kedua.
    auto cut_at_second_model_prefix = [&](const std::string& token) {
        std::size_t first = serial.find(token);
        if (first == std::string::npos) return;
        std::size_t second = serial.find(token, first + token.size());
        if (second != std::string::npos && second > 0) {
            serial.erase(second);
        }
    };
    cut_at_second_model_prefix("DS-");
    cut_at_second_model_prefix("CS-");

    // Guard deterministik: jika prefix model muncul >= 2 kali dalam serial,
    // suffix dari kemunculan terakhir adalah model yang tertempel.
    std::vector<std::size_t> model_prefix_positions;
    auto collect_positions = [&](std::string_view token) {
        std::size_t pos = serial.find(token);
        while (pos != std::string::npos) {
            model_prefix_positions.push_back(pos);
            pos = serial.find(token, pos + token.size());
        }
    };
    collect_positions("DS-");
    collect_positions("CS-");
    if (model_prefix_positions.size() >= 2) {
        const auto last_prefix =
            *std::max_element(model_prefix_positions.begin(), model_prefix_positions.end());
        if (last_prefix > 0 && last_prefix < serial.size()) {
            serial.erase(last_prefix);
        }
    }

    if (!d.model.empty()) {
        auto model = d.model;
        model.erase(
            std::remove_if(model.begin(), model.end(), [](unsigned char ch) {
                return std::iscntrl(ch) != 0;
            }),
            model.end());
        const auto model_pos = serial.rfind(model);
        if (model_pos != std::string::npos && model_pos > 0) {
            serial.erase(model_pos);
        }
    }

    return serial;
}

static void print_device(const Device& d, bool normalize_serial) {
    const auto serial = normalize_serial ? sanitize_serial_for_display(d)
                                         : d.serial_number.get();
    std::cout << std::left
              << std::setw(18) << d.network.ip.get()
              << std::setw(20) << d.mac_address.get()
              << std::setw(36) << serial
              << std::setw(20) << d.model
              << std::setw(12) << d.status_string()
              << std::setw(10) << d.network.http_port.get()
              << d.firmware_version.get()
              << "\n";
}

static void print_interfaces() {
    const auto ifaces = get_active_interfaces();
    std::cout << "Network interfaces yang digunakan:\n";
    for (const auto& iface : ifaces) {
        std::cout << std::format("  {} — {} (broadcast: {})\n",
            iface.name, iface.address.get(), iface.broadcast.get());
    }
    std::cout << "\n";
}

struct CliOptions {
    bool list_ifaces{false};
    bool help{false};
    bool normalize_serial{false};
    int timeout_ms{SadpDiscovery::DEFAULT_TIMEOUT_MS};
    std::optional<std::string> iface_name;
};

static void print_usage(const char* argv0) {
    std::cout << "Usage: " << argv0 << " [options]\n";
    std::cout << "Options:\n";
    std::cout << "  --help              Tampilkan bantuan\n";
    std::cout << "  --list-ifaces       Tampilkan daftar interface dan keluar\n";
    std::cout << "  --iface <name>      Pakai satu interface spesifik (contoh: enx000ec68b9ec5)\n";
    std::cout << "  --timeout-ms <n>    Timeout scan dalam milidetik (default 4500)\n";
    std::cout << "  --normalize-serial  Normalisasi serial untuk tampilan (best-effort)\n";
}

static Result<CliOptions> parse_args(int argc, char* argv[]) {
    CliOptions opts;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            opts.help = true;
        } else if (arg == "--list-ifaces") {
            opts.list_ifaces = true;
        } else if (arg == "--normalize-serial") {
            opts.normalize_serial = true;
        } else if (arg == "--iface") {
            if (i + 1 >= argc) {
                return make_error<CliOptions>(ErrorCode::EmptyInput, "--iface membutuhkan nama interface");
            }
            opts.iface_name = std::string{argv[++i]};
        } else if (arg == "--timeout-ms") {
            if (i + 1 >= argc) {
                return make_error<CliOptions>(ErrorCode::EmptyInput, "--timeout-ms membutuhkan nilai");
            }
            try {
                opts.timeout_ms = std::stoi(argv[++i]);
            } catch (...) {
                return make_error<CliOptions>(ErrorCode::InvalidPort, "nilai --timeout-ms tidak valid");
            }
            if (opts.timeout_ms <= 0 || opts.timeout_ms > 60000) {
                return make_error<CliOptions>(ErrorCode::InvalidPort, "--timeout-ms harus 1..60000");
            }
        } else {
            return make_error<CliOptions>(ErrorCode::UnexpectedResponse, "argumen tidak dikenal: " + arg);
        }
    }
    return make_ok(opts);
}

// ── main ───────────────────────────────────────────────────────────────────

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("HikSADP Linux");
    app.setApplicationVersion("1.0.0");
    const auto log_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir{}.mkpath(log_dir);
    Logger::set_log_file((log_dir + "/hiksadp.log").toStdString());
    Logger::write(LogLevel::Info, "CLI started");

    auto parsed = parse_args(argc, argv);
    if (!parsed) {
        Logger::write(LogLevel::Error, std::string{"CLI arg error: "} + parsed.error().message());
        std::cerr << "Argumen error: " << parsed.error().message() << "\n\n";
        print_usage(argv[0]);
        return 2;
    }
    const auto opts = parsed.value();

    print_header();
    if (opts.help) {
        Logger::write(LogLevel::Info, "CLI help requested");
        print_usage(argv[0]);
        return 0;
    }

    print_interfaces();
    if (opts.list_ifaces) {
        return 0;
    }

    std::cout << "Scanning jaringan...\n\n";

    auto scanner = std::make_unique<SadpDiscovery>();
    scanner->set_timeout(opts.timeout_ms);

    if (opts.iface_name.has_value()) {
        const auto ifaces = get_active_interfaces();
        const auto it = std::find_if(ifaces.begin(), ifaces.end(), [&](const NetworkInterface& iface) {
            return iface.name == opts.iface_name.value();
        });
        if (it == ifaces.end()) {
            std::cerr << "Interface tidak ditemukan/terfilter: " << opts.iface_name.value() << "\n";
            return 3;
        }
        scanner->set_interface(*it);
        std::cout << "Menggunakan interface: " << it->name
                  << " (" << it->address.get() << ", bcast " << it->broadcast.get() << ")\n\n";
    }

    int device_count = 0;
    bool header_printed = false;

    // Callback: device ditemukan satu per satu
    scanner->on_device_found([&](const Device& device) {
        if (!header_printed) {
            print_device_table_header();
            header_printed = true;
        }
        print_device(device, opts.normalize_serial);
        ++device_count;
    });

    // Callback: scan selesai
    scanner->on_scan_complete([&](const DiscoveryResult& result) {
        if (header_printed) {
            print_separator();
        }

        if (device_count == 0) {
            std::cout << "Tidak ada perangkat Hikvision yang ditemukan di jaringan.\n";
            std::cout << std::format("Debug: sent={}, datagrams_in={}, parse_failed={}\n",
                result.packets_sent, result.datagrams_received, result.datagrams_parse_failed);
            std::cout << "Pastikan:\n";
            std::cout << "  1. Perangkat terhubung ke jaringan yang sama\n";
            std::cout << "  2. Firewall tidak memblokir UDP port 37020\n";
            std::cout << "  3. Gunakan --iface <name> ke interface LAN yang benar\n";
        } else {
            std::cout << std::format("\nTotal: {} perangkat ditemukan", device_count);
            std::cout << std::format(" (scan selesai dalam ~{} detik)\n",
                result.packets_sent > 0 ? "4.5" : "0");
            std::cout << std::format("Debug: sent={}, datagrams_in={}, parse_failed={}\n",
                result.packets_sent, result.datagrams_received, result.datagrams_parse_failed);

            // Ringkasan status
            int inactive_count = 0;
            for (const auto& dev : result.devices) {
                if (is_inactive(dev.state)) ++inactive_count;
            }

            if (inactive_count > 0) {
                std::cout << std::format(
                    "\nPeringatan: {} perangkat belum diaktivasi (status: Inactive)\n",
                    inactive_count);
                std::cout << "Gunakan HikSADP Linux GUI untuk mengaktivasi perangkat tersebut.\n";
            }
        }

        std::cout << "\n";

        // Selesai — exit
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
    });

    // Callback: error
    scanner->on_error([&](const AppError& err) {
        std::cerr << std::format("Error: {}\n", err.message());
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
    });

    // Mulai scan
    auto scan_result = scanner->start_scan();
    if (!scan_result) {
        std::cerr << std::format("Gagal memulai scan: {}\n",
                                  scan_result.error().message());
        return 1;
    }

    return app.exec();
}
