// Qt headers di .cpp saja — moc hanya memproses file yang di-list di CMake
#include <QCoreApplication>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QNetworkAddressEntry>
#include <QObject>
#include <QTimer>
#include <QUdpSocket>
#include <QXmlStreamReader>

#include "sadp_discovery.hpp"
#include <chrono>
#include <format>
#include <iostream>
#include <random>
#include <regex>
#include <sstream>
#include <string_view>
#include <cctype>
#include <memory>
#include <unordered_map>

namespace hiksadp::protocol {

namespace {
[[nodiscard]] bool is_empty_inquiry_echo(std::span<const std::byte> raw) noexcept
{
    constexpr std::size_t kSadpHeaderSize = 20;
    constexpr std::size_t kOffsetCommand = 1;
    constexpr std::size_t kOffsetPayloadLen = 16;
    constexpr std::uint8_t kCmdInquiry = 0x01;

    if (raw.size() != kSadpHeaderSize) return false;
    if (static_cast<std::uint8_t>(raw[kOffsetCommand]) != kCmdInquiry) return false;

    const auto payload_len =
        (static_cast<std::uint16_t>(raw[kOffsetPayloadLen]) << 8) |
         static_cast<std::uint16_t>(raw[kOffsetPayloadLen + 1]);
    return payload_len == 0;
}

[[nodiscard]] bool contains_probe_match(std::string_view s)
{
    return s.find("<ProbeMatch") != std::string::npos ||
           s.find("ProbeMatch>") != std::string::npos;
}

[[nodiscard]] std::optional<Device> parse_probe_match_xml(const std::string& payload)
{
    QXmlStreamReader reader(QString::fromStdString(payload));
    if (reader.readNextStartElement()) {
        if (reader.name() != QStringLiteral("ProbeMatch")) {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }

    Device device;
    device.network.http_port = ports::HTTP_DEFAULT;
    device.network.sdk_port = ports::SDK_DEFAULT;
    bool has_ip = false;
    bool has_mac = false;

    while (reader.readNextStartElement()) {
        const auto name = reader.name();
        const auto text = reader.readElementText().trimmed();

        if (name == QStringLiteral("DeviceSN")) {
            device.serial_number = SerialNumber{text.toStdString()};
        } else if (name == QStringLiteral("MAC")) {
            std::string mac = text.toStdString();
            for (char& c : mac) {
                if (c == '-') c = ':';
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            device.mac_address = MacAddress{mac};
            has_mac = !mac.empty();
        } else if (name == QStringLiteral("SoftwareVersion")) {
            device.firmware_version = FirmwareVersion{text.toStdString()};
        } else if (name == QStringLiteral("DeviceDescription")) {
            device.model = text.toStdString();
        } else if (name == QStringLiteral("DeviceType")) {
            device.device_type = text.toStdString();
        } else if (name == QStringLiteral("IPv4Address")) {
            device.network.ip = IpAddress{text.toStdString()};
            has_ip = !text.isEmpty();
        } else if (name == QStringLiteral("IPv4SubnetMask")) {
            device.network.subnet_mask = IpAddress{text.toStdString()};
        } else if (name == QStringLiteral("IPv4Gateway")) {
            device.network.gateway = IpAddress{text.toStdString()};
        } else if (name == QStringLiteral("HttpPort")) {
            bool ok = false;
            const int v = text.toInt(&ok);
            if (ok && v > 0 && v <= 65535) {
                device.network.http_port = Port{static_cast<std::uint16_t>(v)};
            }
        } else if (name == QStringLiteral("CommandPort")) {
            bool ok = false;
            const int v = text.toInt(&ok);
            if (ok && v > 0 && v <= 65535) {
                device.network.sdk_port = Port{static_cast<std::uint16_t>(v)};
            }
        } else if (name == QStringLiteral("Activated")) {
            const auto val = text.toLower();
            device.state = (val == QStringLiteral("true") || val == QStringLiteral("1"))
                ? DeviceState{StateActive{}}
                : DeviceState{StateInactive{}};
        }
    }

    if (!has_ip || !has_mac) {
        return std::nullopt;
    }
    // Normalisasi kasus firmware yang menggabungkan DeviceSN dengan model
    // (contoh: "SERIAL...DS-2CD2025..."), baik DeviceDescription kosong atau terisi.
    if (!device.serial_number.get().empty()) {
        auto serial = device.serial_number.get();
        const auto split_at_second_prefix = [&](std::string_view token) -> std::size_t {
            const auto first = serial.find(token);
            if (first == std::string::npos) return std::string::npos;
            return serial.find(token, first + token.size());
        };

        const auto second_ds = split_at_second_prefix("DS-");
        const auto second_cs = split_at_second_prefix("CS-");
        std::size_t split_pos = std::string::npos;
        if (second_ds != std::string::npos && second_cs != std::string::npos) {
            split_pos = std::min(second_ds, second_cs);
        } else if (second_ds != std::string::npos) {
            split_pos = second_ds;
        } else if (second_cs != std::string::npos) {
            split_pos = second_cs;
        }

        if (split_pos != std::string::npos && split_pos > 0 && split_pos < serial.size()) {
            const auto extracted_model = serial.substr(split_pos);
            serial.erase(split_pos);
            device.serial_number = SerialNumber{serial};
            if (device.model.empty()) {
                device.model = extracted_model;
            }
        } else if (!device.model.empty()) {
            // Kasus model sudah terisi, serial berakhiran model yang sama.
            if (serial.size() > device.model.size() && serial.ends_with(device.model)) {
                serial.erase(serial.size() - device.model.size());
                device.serial_number = SerialNumber{serial};
            }
        }
    }

    if (device.model.empty()) {
        device.model = device.device_type;
    }

    // Beberapa firmware mengembalikan DeviceSN yang ditempel dengan model.
    // Potong mulai kemunculan kedua pattern model-like (DS-/CS-) di serial.
    if (!device.serial_number.get().empty()) {
        auto serial = device.serial_number.get();
        const auto model_pos = (!device.model.empty()) ? serial.find(device.model) : std::string::npos;
        if (model_pos != std::string::npos && model_pos > 0) {
            serial.erase(model_pos);
            device.serial_number = SerialNumber{serial};
        } else {
            const auto find_second_model_prefix = [&](std::string_view s) -> std::size_t {
                const auto pos_ds_first = s.find("DS-");
                const auto pos_cs_first = s.find("CS-");

                auto next_after = [&](std::string_view token, std::size_t first_pos) -> std::size_t {
                    if (first_pos == std::string::npos) return std::string::npos;
                    return s.find(token, first_pos + token.size());
                };

                const auto pos_ds_second = next_after("DS-", pos_ds_first);
                const auto pos_cs_second = next_after("CS-", pos_cs_first);

                if (pos_ds_second == std::string::npos) return pos_cs_second;
                if (pos_cs_second == std::string::npos) return pos_ds_second;
                return std::min(pos_ds_second, pos_cs_second);
            };

            const auto second_pos = find_second_model_prefix(serial);
            if (second_pos != std::string::npos && second_pos > 0 && second_pos < serial.size()) {
                serial.erase(second_pos);
                device.serial_number = SerialNumber{serial};
            }
        }
    }
    if (device.state.valueless_by_exception()) {
        device.state = DeviceState{StateInactive{}};
    }
    device.last_seen = std::chrono::steady_clock::now();
    return device;
}
} // namespace

// ── get_active_interfaces ──────────────────────────────────────────────────

std::vector<NetworkInterface> get_active_interfaces()
{
    auto is_ignored_interface = [](std::string_view name) {
        return name == "lo" ||
               name.rfind("docker", 0) == 0 ||
               name.rfind("br-", 0) == 0 ||
               name.rfind("virbr", 0) == 0 ||
               name.rfind("lxc", 0) == 0 ||
               name.rfind("veth", 0) == 0 ||
               name.rfind("tailscale", 0) == 0 ||
               name.rfind("zt", 0) == 0;
    };

    std::vector<NetworkInterface> result;
    for (const auto& qt_iface : QNetworkInterface::allInterfaces()) {
        const auto iface_name = qt_iface.name().toStdString();
        if (is_ignored_interface(iface_name)) continue;

        const auto flags = qt_iface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp))      continue;
        if (!flags.testFlag(QNetworkInterface::IsRunning)) continue;
        if (flags.testFlag(QNetworkInterface::IsLoopBack)) continue;
        if (flags.testFlag(QNetworkInterface::IsPointToPoint)) continue;

        for (const auto& entry : qt_iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
            if (entry.broadcast().isNull()) continue;

            NetworkInterface iface;
            iface.name      = iface_name;
            iface.address   = IpAddress{entry.ip().toString().toStdString()};
            iface.broadcast = IpAddress{entry.broadcast().toString().toStdString()};
            iface.is_up     = true;
            result.push_back(std::move(iface));
        }
    }
    return result;
}

// ── SadpWorker — QObject tersembunyi di dalam pimpl ───────────────────────

class SadpWorker : public QObject {
    Q_OBJECT
public:
    explicit SadpWorker(QObject* parent = nullptr) : QObject{parent} {}

    std::vector<std::unique_ptr<QUdpSocket>> sockets;
    std::unique_ptr<QTimer>     timeout_timer;
    std::unique_ptr<QTimer>     auto_refresh_timer;
};

// ── SadpDiscovery::Impl ────────────────────────────────────────────────────

struct SadpDiscovery::Impl {
    SadpWorker* worker{nullptr}; // owned by Qt parent chain

    std::optional<NetworkInterface> selected_interface;
    int    timeout_ms{SadpDiscovery::DEFAULT_TIMEOUT_MS};
    bool   auto_refresh{false};
    bool   scanning{false};
    std::uint16_t sequence_number{0};

    std::unordered_map<std::string, Device> current_scan_devices;
    std::unordered_map<std::string, Device> known_devices;
    DiscoveryResult current_result;

    SadpDiscovery::DeviceFoundCallback  cb_device_found;
    SadpDiscovery::ScanCompleteCallback cb_scan_complete;
    SadpDiscovery::ErrorCallback        cb_error;

    // Pointer ke owner untuk memanggil methods
    SadpDiscovery* owner{nullptr};

    void init_worker() {
        worker = new SadpWorker{};
        worker->timeout_timer       = std::make_unique<QTimer>(worker);
        worker->auto_refresh_timer  = std::make_unique<QTimer>(worker);

        worker->timeout_timer->setSingleShot(true);
        worker->auto_refresh_timer->setInterval(SadpDiscovery::AUTO_REFRESH_MS);

        QObject::connect(worker->timeout_timer.get(), &QTimer::timeout,
            [this]() { finalize_scan(); });

        QObject::connect(worker->auto_refresh_timer.get(), &QTimer::timeout,
            [this]() {
                if (!scanning) {
                    auto r = owner->start_scan();
                    if (!r && cb_error) cb_error(r.error());
                }
            });
    }

    Result<void> send_probe_from_socket(QUdpSocket& sock, const NetworkInterface& iface) {
        // 1) SADP biner inquiry (kompatibilitas)
        auto packet_result = PacketBuilder::build_inquiry(sequence_number++);
        if (!packet_result) return std::unexpected(packet_result.error());

        const auto& packet = packet_result.value();
        QByteArray qt_packet(
            reinterpret_cast<const char*>(packet.data()),
            static_cast<qsizetype>(packet.size()));

        // 2) SADP XML probe (format yang dipakai hikvision-tooling)
        const auto now_ns = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto probe_uuid = std::format("hiksadp-{:x}", static_cast<unsigned long long>(now_ns));
        const auto xml_probe_1 = std::format(
            "<?xml version=\"1.0\" encoding=\"utf-8\"?><Probe><Uuid>{}</Uuid><Types>inquiry</Types></Probe>",
            probe_uuid);
        const auto xml_probe_2 = std::format(
            "<?xml version=\"1.0\" encoding=\"utf-8\"?><Probe><Uuid>{}</Uuid><Types>inquiry_v32</Types></Probe>",
            probe_uuid);

        const std::array<QHostAddress, 3> addrs = {
            QHostAddress{QString::fromStdString(iface.broadcast.get())},
            QHostAddress::Broadcast,
            QHostAddress{"239.255.255.250"}
        };

        int sent = 0;
        for (const auto& addr : addrs) {
            // biner
            if (sock.writeDatagram(
                    qt_packet, addr,
                    ports::SADP_DISCOVERY.get()) > 0)
                ++sent;

            // probe xml inquiry
            if (sock.writeDatagram(
                    QByteArray::fromStdString(xml_probe_1),
                    addr,
                    ports::SADP_DISCOVERY.get()) > 0)
                ++sent;

            // probe xml inquiry_v32
            if (sock.writeDatagram(
                    QByteArray::fromStdString(xml_probe_2),
                    addr,
                    ports::SADP_DISCOVERY.get()) > 0)
                ++sent;
        }
        current_result.packets_sent += sent;
        return Result<void>{};
    }

    Result<void> init_scan_sockets() {
        worker->sockets.clear();
        const auto all_ifaces = get_active_interfaces();
        std::vector<NetworkInterface> scan_ifaces;
        if (selected_interface) {
            scan_ifaces.push_back(*selected_interface);
        } else {
            scan_ifaces = all_ifaces;
        }
        if (scan_ifaces.empty()) {
            return make_error<void>(ErrorCode::NetworkUnreachable, "tidak ada interface aktif");
        }

        for (const auto& iface : scan_ifaces) {
            auto sock = std::make_unique<QUdpSocket>(worker);
            const auto local_addr = QHostAddress{QString::fromStdString(iface.address.get())};
            if (!sock->bind(local_addr, 0, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
                continue;
            }
            QObject::connect(sock.get(), &QUdpSocket::readyRead, [this, s = sock.get()]() {
                on_ready_read(*s);
            });
            auto probe_result = send_probe_from_socket(*sock, iface);
            if (!probe_result) {
                continue;
            }
            worker->sockets.push_back(std::move(sock));
        }

        if (worker->sockets.empty() || current_result.packets_sent == 0) {
            return make_error<void>(ErrorCode::BroadcastFailed, "gagal kirim probe dari semua interface");
        }
        return Result<void>{};
    }

    void on_ready_read(QUdpSocket& socket) {
        while (socket.hasPendingDatagrams()) {
            QByteArray  datagram;
            datagram.resize(static_cast<qsizetype>(
                socket.pendingDatagramSize()));
            socket.readDatagram(datagram.data(), datagram.size(), nullptr, nullptr);
            ++current_result.datagrams_received;
            process_datagram(datagram);
        }
    }

    void process_datagram(const QByteArray& data) {
        const auto payload = data.toStdString();
        if (contains_probe_match(payload)) {
            auto parsed = parse_probe_match_xml(payload);
            if (parsed) {
                const auto mac_key = parsed->mac_address.get();
                if (!current_scan_devices.contains(mac_key)) {
                    current_scan_devices.emplace(mac_key, *parsed);
                    known_devices[mac_key] = *parsed;
                    ++current_result.responses_received;
                    if (cb_device_found) cb_device_found(*parsed);
                }
            }
            return;
        }

        std::span<const std::byte> raw{
            reinterpret_cast<const std::byte*>(data.constData()),
            static_cast<std::size_t>(data.size())};

        if (is_empty_inquiry_echo(raw)) {
            return;
        }

        auto parse_result = PacketParser::parse_inquiry_reply(raw);
        if (!parse_result) {
            ++current_result.datagrams_parse_failed;
            if (current_result.datagrams_parse_failed <= 3) {
                std::cerr << "[SADP parser] drop: "
                          << parse_result.error().message()
                          << " | " << PacketParser::debug_summary(raw)
                          << "\n";
            }
            return;
        }

        const auto& info    = parse_result.value();
        const auto  mac_key = info.mac_address.get();
        if (current_scan_devices.contains(mac_key)) return;

        Device device;
        device.serial_number    = info.serial_number;
        device.mac_address      = info.mac_address;
        device.firmware_version = info.firmware_version;
        device.model            = info.model;
        device.device_type      = info.device_type;
        device.last_seen        = std::chrono::steady_clock::now();
        device.network.ip          = info.ip_address;
        device.network.subnet_mask = info.subnet_mask;
        device.network.gateway     = info.gateway;
        device.network.http_port   = info.http_port;
        device.network.sdk_port    = info.sdk_port;
        device.network.dhcp_enabled = info.dhcp_enabled;
        device.state = info.is_inactive
            ? DeviceState{StateInactive{}}
            : DeviceState{StateActive{}};

        current_scan_devices.emplace(mac_key, device);
        known_devices[mac_key] = device;
        ++current_result.responses_received;

        if (cb_device_found) cb_device_found(device);
    }

    void finalize_scan() {
        scanning = false;
        current_result.devices.clear();
        current_result.devices.reserve(current_scan_devices.size());
        for (const auto& [_, dev] : current_scan_devices)
            current_result.devices.push_back(dev);

        if (cb_scan_complete) cb_scan_complete(current_result);

        if (auto_refresh && !worker->auto_refresh_timer->isActive())
            worker->auto_refresh_timer->start();
    }
};

// ── SadpDiscovery public methods ───────────────────────────────────────────

SadpDiscovery::SadpDiscovery()
    : impl_{std::make_unique<Impl>()}
{
    impl_->owner = this;
    impl_->init_worker();
}

SadpDiscovery::~SadpDiscovery() { stop(); }

void SadpDiscovery::set_interface(const NetworkInterface& iface) {
    impl_->selected_interface = iface;
}
void SadpDiscovery::set_timeout(int ms)   { impl_->timeout_ms = ms; }
void SadpDiscovery::set_auto_refresh(bool en) {
    impl_->auto_refresh = en;
    if (en) impl_->worker->auto_refresh_timer->start();
    else    impl_->worker->auto_refresh_timer->stop();
}

void SadpDiscovery::on_device_found(DeviceFoundCallback cb)
    { impl_->cb_device_found = std::move(cb); }
void SadpDiscovery::on_scan_complete(ScanCompleteCallback cb)
    { impl_->cb_scan_complete = std::move(cb); }
void SadpDiscovery::on_error(ErrorCallback cb)
    { impl_->cb_error = std::move(cb); }

Result<void> SadpDiscovery::start_scan() {
    if (impl_->scanning) return Result<void>{};

    impl_->current_scan_devices.clear();
    impl_->current_result = DiscoveryResult{};
    impl_->current_result.scan_time = std::chrono::steady_clock::now();
    impl_->current_result.interface_used =
        impl_->selected_interface ? impl_->selected_interface->name : "all";
    impl_->scanning = true;

    auto r = impl_->init_scan_sockets();
    if (!r) {
        impl_->scanning = false;
        if (impl_->cb_error) impl_->cb_error(r.error());
        return r;
    }

    impl_->worker->timeout_timer->start(impl_->timeout_ms);
    return Result<void>{};
}

void SadpDiscovery::stop() {
    impl_->worker->timeout_timer->stop();
    impl_->worker->auto_refresh_timer->stop();
    impl_->scanning = false;
    for (auto& sock : impl_->worker->sockets) {
        if (sock && sock->state() != QAbstractSocket::UnconnectedState) {
            sock->close();
        }
    }
    impl_->worker->sockets.clear();
}

bool SadpDiscovery::is_scanning() const noexcept { return impl_->scanning; }

std::vector<Device> SadpDiscovery::last_discovered() const {
    std::vector<Device> out;
    out.reserve(impl_->known_devices.size());
    for (const auto& [_, dev] : impl_->known_devices)
        out.push_back(dev);
    return out;
}

} // namespace hiksadp::protocol

// moc requires this at end of .cpp when using Q_OBJECT inside .cpp
#include "sadp_discovery.moc"
