#include "device_manager.hpp"
#include <QByteArray>
#include <QHostAddress>
#include <QUdpSocket>
#include <algorithm>
#include <format>
#include <cstdio>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace hiksadp {

namespace {
std::string escape_xml_text(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    for (const char c : in) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

Result<void> send_sadp_reset_command(const Device& dev,
                                     const std::string& reset_code,
                                     const Password& new_password)
{
    if (reset_code.empty()) {
        return make_error<void>(ErrorCode::EmptyInput, "reset code kosong");
    }
    if (!is_strong_password(new_password.get())) {
        return make_error<void>(ErrorCode::WeakPassword);
    }

    QUdpSocket socket;
    if (!socket.bind(QHostAddress::AnyIPv4, 0,
                     QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        return make_error<void>(ErrorCode::SocketBindFailed, socket.errorString().toStdString());
    }

    const auto xml = std::format(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<Probe>"
        "<Uuid>hiksadp-reset</Uuid>"
        "<MAC>{}</MAC>"
        "<Types>securityCode</Types>"
        "<SecurityCode>{}</SecurityCode>"
        "<Password>{}</Password>"
        "</Probe>",
        dev.mac_address.get(),
        escape_xml_text(reset_code),
        escape_xml_text(new_password.get()));

    const auto target_ip = QHostAddress{QString::fromStdString(dev.network.ip.get())};
    const auto sent = socket.writeDatagram(QByteArray::fromStdString(xml), target_ip, ports::SADP_DISCOVERY.get());
    if (sent <= 0) {
        return make_error<void>(ErrorCode::BroadcastFailed, socket.errorString().toStdString());
    }
    if (!socket.waitForReadyRead(5000)) {
        return make_error<void>(
            ErrorCode::OperationTimeout,
            "tidak ada respons SADP reset (fitur mungkin tidak didukung firmware ini)");
    }

    QByteArray datagram;
    datagram.resize(static_cast<qsizetype>(socket.pendingDatagramSize()));
    QHostAddress from_addr;
    quint16 from_port = 0;
    socket.readDatagram(datagram.data(), datagram.size(), &from_addr, &from_port);
    const auto resp = QString::fromUtf8(datagram).toStdString();

    const auto from_same = from_addr.toString().toStdString() == dev.network.ip.get();
    if (!from_same) {
        return make_error<void>(ErrorCode::UnexpectedResponse, "menerima respons dari device lain");
    }

    const auto ok_hit =
        (resp.find("<Result>OK</Result>") != std::string::npos) ||
        (resp.find("<statusValue>200</statusValue>") != std::string::npos) ||
        (resp.find("<SubStatusCode>ok</SubStatusCode>") != std::string::npos);
    if (ok_hit) {
        return Result<void>{};
    }

    // Capability/error detection dari payload response.
    if (resp.find("notSupport") != std::string::npos ||
        resp.find("not support") != std::string::npos ||
        resp.find("OperationNotSupported") != std::string::npos) {
        return make_error<void>(
            ErrorCode::UnexpectedResponse,
            "password reset via SADP tidak didukung device/firmware ini");
    }
    if (resp.find("Invalid SecurityCode") != std::string::npos ||
        resp.find("invalid security code") != std::string::npos) {
        return make_error<void>(ErrorCode::AuthenticationFailed, "security code tidak valid");
    }
    if (resp.find("password") != std::string::npos &&
        resp.find("weak") != std::string::npos) {
        return make_error<void>(ErrorCode::WeakPassword);
    }

    return make_error<void>(
        ErrorCode::UnexpectedResponse,
        "respons reset SADP tidak dikenali");
}
} // namespace

// ── Helpers ────────────────────────────────────────────────────────────────

// Increment IP address by offset (mis. 192.168.1.64 + 2 = 192.168.1.66)
IpAddress DeviceManager::increment_ip(const IpAddress& ip, int offset)
{
    unsigned int a, b, c, d;
    if (std::sscanf(ip.get().c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        return ip; // fallback
    }
    const unsigned int full = (a << 24) | (b << 16) | (c << 8) | d;
    const unsigned int incremented = full + static_cast<unsigned int>(offset);
    return IpAddress{std::format("{}.{}.{}.{}",
        (incremented >> 24) & 0xFF,
        (incremented >> 16) & 0xFF,
        (incremented >> 8)  & 0xFF,
         incremented        & 0xFF)};
}

// ── DeviceManager::Impl ────────────────────────────────────────────────────

struct DeviceManager::Impl {
    mutable std::mutex mutex;
    std::unordered_map<std::string, Device> device_map; // key = MAC string

    IsapiClient isapi;

    DeviceManager::DeviceListChangedCallback cb_list_changed;
    DeviceManager::ProgressCallback          cb_progress;

    [[nodiscard]] std::optional<Device>
    find_device(const MacAddress& mac) const {
        const auto it = device_map.find(mac.get());
        if (it == device_map.end()) return std::nullopt;
        return it->second;
    }

    [[nodiscard]] std::vector<Device> snapshot_devices() const {
        std::vector<Device> list;
        list.reserve(device_map.size());
        for (const auto& [_, dev] : device_map) {
            list.push_back(dev);
        }
        return list;
    }
};

// ── DeviceManager public API ───────────────────────────────────────────────

DeviceManager::DeviceManager() : impl_{std::make_unique<Impl>()} {}
DeviceManager::~DeviceManager() = default;

void DeviceManager::on_device_list_changed(DeviceListChangedCallback cb) {
    impl_->cb_list_changed = std::move(cb);
}
void DeviceManager::on_progress(ProgressCallback cb) {
    impl_->cb_progress = std::move(cb);
}

void DeviceManager::update_devices(const std::vector<Device>& devices) {
    DeviceListChangedCallback cb;
    std::vector<Device> snapshot;
    {
        std::lock_guard lock{impl_->mutex};
        for (const auto& dev : devices) {
            impl_->device_map[dev.mac_address.get()] = dev;
        }
        cb = impl_->cb_list_changed;
        snapshot = impl_->snapshot_devices();
    }
    if (cb) cb(snapshot);
}

std::vector<Device> DeviceManager::devices() const {
    std::lock_guard lock{impl_->mutex};
    std::vector<Device> out;
    out.reserve(impl_->device_map.size());
    for (const auto& [_, dev] : impl_->device_map)
        out.push_back(dev);
    return out;
}

std::optional<Device> DeviceManager::find_by_mac(const MacAddress& mac) const {
    std::lock_guard lock{impl_->mutex};
    return impl_->find_device(mac);
}

// ── Aktivasi ──────────────────────────────────────────────────────────────

Result<void> DeviceManager::activate_device(const MacAddress& mac,
                                              const Password& password)
{
    std::optional<Device> dev;
    {
        std::lock_guard lock{impl_->mutex};
        dev = impl_->find_device(mac);
    }
    if (!dev) return make_error<void>(ErrorCode::DeviceNotFound, mac.get());
    if (is_active(dev->state))
        return make_error<void>(ErrorCode::DeviceAlreadyActive);

    ActivationRequest req;
    req.ip           = dev->network.ip;
    req.http_port    = dev->network.http_port;
    req.new_password = password;

    auto result = impl_->isapi.activate_device(req);
    if (result) {
        DeviceListChangedCallback cb;
        std::vector<Device> snapshot;
        {
            std::lock_guard lock{impl_->mutex};
            impl_->device_map[mac.get()].state = StateActive{};
            cb = impl_->cb_list_changed;
            snapshot = impl_->snapshot_devices();
        }
        if (cb) cb(snapshot);
    }
    return result;
}

BatchResult DeviceManager::activate_batch(const std::vector<MacAddress>& macs,
                                           const Password& password)
{
    BatchResult batch;
    batch.items.reserve(macs.size());
    int i = 0;

    for (const auto& mac : macs) {
        ++i;
        BatchItemResult item;
        item.mac = mac;

        auto dev = find_by_mac(mac);
        item.device_label = dev
            ? std::format("{} ({})", dev->model, dev->network.ip.get())
            : mac.get();

        const auto cb = impl_->cb_progress;
        if (cb) cb(i, static_cast<int>(macs.size()), "Aktivasi: " + item.device_label);

        auto result = activate_device(mac, password);
        item.success = result.has_value();
        if (!result) item.error_message = result.error().message();

        batch.items.push_back(std::move(item));
    }
    return batch;
}

// ── Network Config ─────────────────────────────────────────────────────────

Result<void> DeviceManager::set_network_config(
    const MacAddress& mac,
    const Password&   password,
    const IpAddress&  new_ip,
    const IpAddress&  new_mask,
    const IpAddress&  new_gateway,
    Port              new_http_port,
    Port              new_sdk_port,
    bool              dhcp_enabled)
{
    std::optional<Device> dev;
    {
        std::lock_guard lock{impl_->mutex};
        dev = impl_->find_device(mac);
    }
    if (!dev) return make_error<void>(ErrorCode::DeviceNotFound, mac.get());
    if (is_inactive(dev->state))
        return make_error<void>(ErrorCode::DeviceInactive);

    NetworkConfigRequest req;
    req.credential    = IsapiCredential{dev->network.ip,
                                         dev->network.http_port,
                                         password};
    req.new_ip        = new_ip;
    req.new_mask      = new_mask;
    req.new_gateway   = new_gateway;
    req.new_http_port = new_http_port;
    req.new_sdk_port  = new_sdk_port;
    req.dhcp_enabled  = dhcp_enabled;

    auto result = impl_->isapi.set_network_config(req);
    if (result) {
        DeviceListChangedCallback cb;
        std::vector<Device> snapshot;
        {
            std::lock_guard lock{impl_->mutex};
            auto& stored               = impl_->device_map[mac.get()];
            stored.network.ip          = new_ip;
            stored.network.subnet_mask = new_mask;
            stored.network.gateway     = new_gateway;
            stored.network.http_port   = new_http_port;
            stored.network.sdk_port    = new_sdk_port;
            stored.network.dhcp_enabled = dhcp_enabled;
            cb = impl_->cb_list_changed;
            snapshot = impl_->snapshot_devices();
        }
        if (cb) cb(snapshot);
    }
    return result;
}

std::vector<std::pair<MacAddress, IpAddress>>
DeviceManager::preview_sequential_ips(const std::vector<MacAddress>& macs,
                                        const IpSequenceConfig& config) const
{
    std::vector<std::pair<MacAddress, IpAddress>> preview;
    preview.reserve(macs.size());
    int offset = 0;
    for (const auto& mac : macs) {
        preview.emplace_back(mac, increment_ip(config.start_ip, offset++));
    }
    return preview;
}

BatchResult DeviceManager::assign_sequential_ips(
    const std::vector<MacAddress>& macs,
    const Password&                password,
    const IpSequenceConfig&        config)
{
    BatchResult batch;
    batch.items.reserve(macs.size());

    for (int i = 0; i < static_cast<int>(macs.size()); ++i) {
        const auto& mac    = macs[static_cast<std::size_t>(i)];
        const auto  new_ip = increment_ip(config.start_ip, i);

        BatchItemResult item;
        item.mac = mac;
        auto dev = find_by_mac(mac);
        item.device_label = dev
            ? std::format("{} ({}→{})", dev->model,
                          dev->network.ip.get(), new_ip.get())
            : mac.get();

        const auto cb = impl_->cb_progress;
        if (cb) cb(i + 1, static_cast<int>(macs.size()), "Set IP: " + item.device_label);

        auto result = set_network_config(mac, password, new_ip,
                                          config.subnet_mask, config.gateway,
                                          config.http_port, config.sdk_port,
                                          config.dhcp_enabled);
        item.success = result.has_value();
        if (!result) item.error_message = result.error().message();
        batch.items.push_back(std::move(item));
    }
    return batch;
}

// ── Reboot ─────────────────────────────────────────────────────────────────

Result<void> DeviceManager::reboot_device(const MacAddress& mac,
                                            const Password& password)
{
    std::optional<Device> dev;
    {
        std::lock_guard lock{impl_->mutex};
        dev = impl_->find_device(mac);
    }
    if (!dev) return make_error<void>(ErrorCode::DeviceNotFound, mac.get());
    if (is_inactive(dev->state))
        return make_error<void>(ErrorCode::DeviceInactive);

    IsapiCredential cred{dev->network.ip, dev->network.http_port, password};
    return impl_->isapi.reboot_device(cred);
}

Result<void> DeviceManager::change_admin_password(const MacAddress& mac,
                                                   const Password& old_password,
                                                   const Password& new_password)
{
    std::optional<Device> dev;
    {
        std::lock_guard lock{impl_->mutex};
        dev = impl_->find_device(mac);
    }
    if (!dev) return make_error<void>(ErrorCode::DeviceNotFound, mac.get());
    if (is_inactive(dev->state))
        return make_error<void>(ErrorCode::DeviceInactive);
    if (!is_strong_password(new_password.get()))
        return make_error<void>(ErrorCode::WeakPassword);

    ChangePasswordRequest req;
    req.credential = IsapiCredential{dev->network.ip, dev->network.http_port, old_password};
    req.target_username = "admin";
    req.new_password = new_password;
    return impl_->isapi.change_password(req);
}

BatchResult DeviceManager::reboot_batch(const std::vector<MacAddress>& macs,
                                          const Password& password)
{
    BatchResult batch;
    batch.items.reserve(macs.size());
    int i = 0;
    for (const auto& mac : macs) {
        ++i;
        BatchItemResult item;
        item.mac = mac;
        auto dev = find_by_mac(mac);
        item.device_label = dev
            ? std::format("{} ({})", dev->model, dev->network.ip.get())
            : mac.get();

        const auto cb = impl_->cb_progress;
        if (cb) cb(i, static_cast<int>(macs.size()), "Reboot: " + item.device_label);
        auto result = reboot_device(mac, password);
        item.success = result.has_value();
        if (!result) item.error_message = result.error().message();
        batch.items.push_back(std::move(item));
    }
    return batch;
}

Result<void> DeviceManager::apply_password_reset_code(const MacAddress& mac,
                                                       const std::string& reset_code,
                                                       const Password& new_password)
{
    std::optional<Device> dev;
    {
        std::lock_guard lock{impl_->mutex};
        dev = impl_->find_device(mac);
    }
    if (!dev) {
        return make_error<void>(ErrorCode::DeviceNotFound, mac.get());
    }
    return send_sadp_reset_command(*dev, reset_code, new_password);
}

// ── Export ─────────────────────────────────────────────────────────────────

Result<std::string> DeviceManager::export_csv() const
{
    std::lock_guard lock{impl_->mutex};
    std::ostringstream ss;
    ss << "IP Address,Subnet Mask,Gateway,HTTP Port,SDK Port,MAC Address,"
          "Serial Number,Model,Device Type,Firmware,DHCP,Status\n";

    for (const auto& [_, dev] : impl_->device_map) {
        ss << dev.network.ip.get()          << ","
           << dev.network.subnet_mask.get() << ","
           << dev.network.gateway.get()     << ","
           << dev.network.http_port.get()   << ","
           << dev.network.sdk_port.get()    << ","
           << dev.mac_address.get()         << ","
           << dev.serial_number.get()       << ","
           << dev.model                     << ","
           << dev.device_type               << ","
           << dev.firmware_version.get()    << ","
           << (dev.network.dhcp_enabled ? "Yes" : "No") << ","
           << dev.status_string()           << "\n";
    }
    return make_ok(ss.str());
}

Result<std::string> DeviceManager::export_xml() const
{
    std::lock_guard lock{impl_->mutex};
    std::ostringstream ss;
    ss << R"(<?xml version="1.0" encoding="UTF-8"?>)" << "\n";
    ss << "<DeviceList>\n";

    for (const auto& [_, dev] : impl_->device_map) {
        ss << "  <Device>\n"
           << "    <IPAddress>"      << dev.network.ip.get()          << "</IPAddress>\n"
           << "    <SubnetMask>"     << dev.network.subnet_mask.get() << "</SubnetMask>\n"
           << "    <Gateway>"        << dev.network.gateway.get()     << "</Gateway>\n"
           << "    <HTTPPort>"       << dev.network.http_port.get()   << "</HTTPPort>\n"
           << "    <SDKPort>"        << dev.network.sdk_port.get()    << "</SDKPort>\n"
           << "    <MACAddress>"     << dev.mac_address.get()         << "</MACAddress>\n"
           << "    <SerialNumber>"   << dev.serial_number.get()       << "</SerialNumber>\n"
           << "    <Model>"          << dev.model                     << "</Model>\n"
           << "    <DeviceType>"     << dev.device_type               << "</DeviceType>\n"
           << "    <Firmware>"       << dev.firmware_version.get()    << "</Firmware>\n"
           << "    <DHCP>"           << (dev.network.dhcp_enabled ? "true" : "false") << "</DHCP>\n"
           << "    <Status>"         << dev.status_string()           << "</Status>\n"
           << "  </Device>\n";
    }
    ss << "</DeviceList>\n";
    return make_ok(ss.str());
}

} // namespace hiksadp
