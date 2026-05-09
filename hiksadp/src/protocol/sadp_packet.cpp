#include "sadp_packet.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <format>
#include <numeric>
#include <regex>
#include <sstream>

namespace hiksadp::protocol {

// ── Checksum ───────────────────────────────────────────────────────────────
//
// Algoritma checksum SADP: XOR semua byte kecuali byte checksum itu sendiri.
// Referensi: MatrixEditor/hiktools reverse-engineering.
//
std::uint8_t PacketBuilder::calculate_checksum(
    std::span<const std::byte> data) noexcept
{
    std::uint8_t cs = 0;
    for (auto b : data) {
        cs ^= static_cast<std::uint8_t>(b);
    }
    return cs;
}

// ── Helpers internal ───────────────────────────────────────────────────────

// Tulis uint16 big-endian ke buffer di offset tertentu
static void write_u16_be(std::vector<std::byte>& buf,
                          std::size_t offset,
                          std::uint16_t value) noexcept
{
    buf[offset]     = static_cast<std::byte>((value >> 8) & 0xFF);
    buf[offset + 1] = static_cast<std::byte>(value & 0xFF);
}


// Baca uint16 big-endian dari span
static std::uint16_t read_u16_be(std::span<const std::byte> data,
                                  std::size_t offset) noexcept
{
    return (static_cast<std::uint16_t>(data[offset]) << 8)
         | static_cast<std::uint16_t>(data[offset + 1]);
}


// Parse MAC address string dari 6 bytes raw
static MacAddress parse_mac_bytes(std::span<const std::byte> data,
                                   std::size_t offset) noexcept
{
    return MacAddress{std::format("{:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X}",
        static_cast<unsigned>(data[offset]),
        static_cast<unsigned>(data[offset + 1]),
        static_cast<unsigned>(data[offset + 2]),
        static_cast<unsigned>(data[offset + 3]),
        static_cast<unsigned>(data[offset + 4]),
        static_cast<unsigned>(data[offset + 5]))};
}

// Encode MAC address string ke 6 bytes
static Result<std::array<std::byte, 6>>
encode_mac(const MacAddress& mac)
{
    if (!is_valid_mac(mac.get())) {
        return make_error<std::array<std::byte, 6>>(
            ErrorCode::InvalidMacAddress, mac.get());
    }

    std::array<std::byte, 6> out{};
    unsigned int bytes[6] = {};
    // sscanf aman di sini karena kita sudah validasi format dengan is_valid_mac
    if (std::sscanf(mac.get().c_str(),
                    "%02X:%02X:%02X:%02X:%02X:%02X",
                    &bytes[0], &bytes[1], &bytes[2],
                    &bytes[3], &bytes[4], &bytes[5]) != 6) {
        // Coba format dengan dash
        if (std::sscanf(mac.get().c_str(),
                        "%02X-%02X-%02X-%02X-%02X-%02X",
                        &bytes[0], &bytes[1], &bytes[2],
                        &bytes[3], &bytes[4], &bytes[5]) != 6) {
            return make_error<std::array<std::byte, 6>>(
                ErrorCode::InvalidMacAddress, mac.get());
        }
    }
    for (int i = 0; i < 6; ++i) {
        out[static_cast<std::size_t>(i)] = static_cast<std::byte>(bytes[i]);
    }
    return Result<std::array<std::byte, 6>>{out};
}

// ── SADP Header Layout (20 bytes) ─────────────────────────────────────────
//
// Offset  Size  Field
// 0       1     Version (0x02)
// 1       1     Command type
// 2       2     Sequence number (big-endian)
// 4       6     Source MAC (FF:FF:FF:FF:FF:FF untuk broadcast)
// 10      6     Destination MAC (FF:FF:FF:FF:FF:FF untuk broadcast)
// 16      2     Payload length (big-endian)
// 18      1     Checksum
// 19      1     Reserved (0x00)
// 20+           XML payload
//
static constexpr std::size_t OFFSET_VERSION      = 0;
static constexpr std::size_t OFFSET_COMMAND      = 1;
static constexpr std::size_t OFFSET_SEQUENCE     = 2;
static constexpr std::size_t OFFSET_SRC_MAC      = 4;
static constexpr std::size_t OFFSET_DST_MAC      = 10;
static constexpr std::size_t OFFSET_PAYLOAD_LEN  = 16;
static constexpr std::size_t OFFSET_CHECKSUM     = 18;
static constexpr std::size_t OFFSET_RESERVED     = 19;
static constexpr std::size_t OFFSET_PAYLOAD      = 20;

static constexpr std::array<std::byte, 6> BROADCAST_MAC = {
    std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF},
    std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}
};

// Bangun header SADP ke dalam buffer yang sudah dialokasi
static void build_header(std::vector<std::byte>& buf,
                          SadpCommand cmd,
                          std::uint16_t seq,
                          const std::array<std::byte, 6>& dst_mac,
                          std::uint16_t payload_len) noexcept
{
    buf[OFFSET_VERSION]  = static_cast<std::byte>(SADP_VERSION);
    buf[OFFSET_COMMAND]  = static_cast<std::byte>(cmd);
    write_u16_be(buf, OFFSET_SEQUENCE, seq);

    // Source MAC: broadcast
    std::copy(BROADCAST_MAC.begin(), BROADCAST_MAC.end(),
              buf.begin() + static_cast<std::ptrdiff_t>(OFFSET_SRC_MAC));

    // Destination MAC
    std::copy(dst_mac.begin(), dst_mac.end(),
              buf.begin() + static_cast<std::ptrdiff_t>(OFFSET_DST_MAC));

    write_u16_be(buf, OFFSET_PAYLOAD_LEN, payload_len);
    buf[OFFSET_RESERVED] = std::byte{0x00};
}

// Finalisasi checksum setelah payload ditulis
static void finalize_checksum(std::vector<std::byte>& buf) noexcept
{
    // Hitung checksum atas semua byte kecuali byte checksum itu sendiri
    std::vector<std::byte> for_checksum;
    for_checksum.reserve(buf.size() - 1);
    for (std::size_t i = 0; i < buf.size(); ++i) {
        if (i != OFFSET_CHECKSUM) {
            for_checksum.push_back(buf[i]);
        }
    }
    buf[OFFSET_CHECKSUM] = static_cast<std::byte>(
        PacketBuilder::calculate_checksum(for_checksum));
}

// ── PacketBuilder implementations ─────────────────────────────────────────

[[nodiscard]] Result<std::vector<std::byte>>
PacketBuilder::build_inquiry(std::uint16_t sequence_number)
{
    // Inquiry tidak membutuhkan payload XML — hanya header
    std::vector<std::byte> buf(SADP_HEADER_SIZE, std::byte{0x00});
    build_header(buf, SadpCommand::Inquiry, sequence_number,
                 BROADCAST_MAC, 0);
    finalize_checksum(buf);
    return make_ok(std::move(buf));
}

[[nodiscard]] Result<std::vector<std::byte>>
PacketBuilder::build_activate(const MacAddress& target_mac,
                               const Password& new_password)
{
    if (!is_valid_mac(target_mac.get())) {
        return make_error<std::vector<std::byte>>(
            ErrorCode::InvalidMacAddress, target_mac.get());
    }
    if (!is_strong_password(new_password.get())) {
        return make_error<std::vector<std::byte>>(ErrorCode::WeakPassword);
    }

    auto mac_bytes_result = encode_mac(target_mac);
    if (!mac_bytes_result) {
        return std::unexpected(mac_bytes_result.error());
    }

    // Payload XML untuk aktivasi
    const std::string xml = std::format(
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<ActivateRequest>)"
        R"(<password>{}</password>)"
        R"(</ActivateRequest>)",
        new_password.get()
    );

    const auto payload_len = static_cast<std::uint16_t>(xml.size());
    std::vector<std::byte> buf(SADP_HEADER_SIZE + xml.size(), std::byte{0x00});

    build_header(buf, SadpCommand::Activate, 0,
                 mac_bytes_result.value(), payload_len);

    // Tulis XML payload
    std::transform(xml.begin(), xml.end(),
                   buf.begin() + OFFSET_PAYLOAD,
                   [](char c) { return static_cast<std::byte>(c); });

    finalize_checksum(buf);
    return make_ok(std::move(buf));
}

[[nodiscard]] Result<std::vector<std::byte>>
PacketBuilder::build_modify_network(
    const MacAddress& target_mac,
    const Password&   password,
    const IpAddress&  new_ip,
    const IpAddress&  new_mask,
    const IpAddress&  new_gateway,
    Port              new_sdk_port,
    Port              new_http_port,
    bool              dhcp_enabled)
{
    if (!is_valid_mac(target_mac.get())) {
        return make_error<std::vector<std::byte>>(
            ErrorCode::InvalidMacAddress, target_mac.get());
    }
    if (!is_valid_ip(new_ip.get())) {
        return make_error<std::vector<std::byte>>(
            ErrorCode::InvalidIpAddress, new_ip.get());
    }
    if (!is_valid_ip(new_mask.get())) {
        return make_error<std::vector<std::byte>>(
            ErrorCode::InvalidIpAddress, "subnet mask: " + new_mask.get());
    }
    if (!is_valid_ip(new_gateway.get())) {
        return make_error<std::vector<std::byte>>(
            ErrorCode::InvalidIpAddress, "gateway: " + new_gateway.get());
    }
    if (!is_valid_port(new_sdk_port.get())) {
        return make_error<std::vector<std::byte>>(ErrorCode::InvalidPort);
    }
    if (!is_valid_port(new_http_port.get())) {
        return make_error<std::vector<std::byte>>(ErrorCode::InvalidPort);
    }

    auto mac_bytes_result = encode_mac(target_mac);
    if (!mac_bytes_result) {
        return std::unexpected(mac_bytes_result.error());
    }

    const std::string xml = std::format(
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<NetworkInterface>)"
        R"(<id>1</id>)"
        R"(<IPAddress>)"
        R"(<ipVersion>v4</ipVersion>)"
        R"(<addressingType>{}</addressingType>)"
        R"(<ipAddress>{}</ipAddress>)"
        R"(<subnetMask>{}</subnetMask>)"
        R"(<DefaultGateway>)"
        R"(<ipAddress>{}</ipAddress>)"
        R"(</DefaultGateway>)"
        R"(</IPAddress>)"
        R"(<httpPort>{}</httpPort>)"
        R"(<sdkPort>{}</sdkPort>)"
        R"(<password>{}</password>)"
        R"(</NetworkInterface>)",
        dhcp_enabled ? "dynamic" : "static",
        new_ip.get(),
        new_mask.get(),
        new_gateway.get(),
        new_http_port.get(),
        new_sdk_port.get(),
        password.get()
    );

    const auto payload_len = static_cast<std::uint16_t>(xml.size());
    std::vector<std::byte> buf(SADP_HEADER_SIZE + xml.size(), std::byte{0x00});

    build_header(buf, SadpCommand::ModifyNet, 0,
                 mac_bytes_result.value(), payload_len);

    std::transform(xml.begin(), xml.end(),
                   buf.begin() + OFFSET_PAYLOAD,
                   [](char c) { return static_cast<std::byte>(c); });

    finalize_checksum(buf);
    return make_ok(std::move(buf));
}

// ── PacketParser implementations ───────────────────────────────────────────

bool PacketParser::verify_checksum(std::span<const std::byte> data) noexcept
{
    if (data.size() < SADP_HEADER_SIZE) return false;

    std::vector<std::byte> for_checksum;
    for_checksum.reserve(data.size() - 1);
    for (std::size_t i = 0; i < data.size(); ++i) {
        if (i != OFFSET_CHECKSUM) {
            for_checksum.push_back(data[i]);
        }
    }
    const auto expected = PacketBuilder::calculate_checksum(for_checksum);
    const auto actual   = static_cast<std::uint8_t>(data[OFFSET_CHECKSUM]);
    return expected == actual;
}

Result<std::string>
PacketParser::extract_xml_payload(std::span<const std::byte> data)
{
    if (data.size() < SADP_HEADER_SIZE) {
        return make_error<std::string>(ErrorCode::InvalidPacket,
                                       "packet terlalu pendek");
    }

    const auto payload_len = read_u16_be(data, OFFSET_PAYLOAD_LEN);
    if (data.size() < SADP_HEADER_SIZE + payload_len) {
        return make_error<std::string>(ErrorCode::InvalidPacket,
                                       "payload length tidak sesuai");
    }

    std::string xml;
    xml.reserve(payload_len);
    for (std::size_t i = 0; i < payload_len; ++i) {
        xml.push_back(
            static_cast<char>(data[SADP_HEADER_SIZE + i]));
    }
    return make_ok(std::move(xml));
}

// Parse nilai sederhana dari XML dengan regex
// Untuk production, pertimbangkan Qt QXmlStreamReader
static std::string extract_xml_value(std::string_view xml,
                                      std::string_view tag)
{
    const std::string open_tag  = "<"  + std::string{tag} + ">";
    const std::string close_tag = "</" + std::string{tag} + ">";

    const auto start = xml.find(open_tag);
    if (start == std::string_view::npos) return {};

    const auto value_start = start + open_tag.size();
    const auto end = xml.find(close_tag, value_start);
    if (end == std::string_view::npos) return {};

    return std::string{xml.substr(value_start, end - value_start)};
}

static std::string extract_xml_value_any(
    std::string_view xml,
    std::initializer_list<std::string_view> tags)
{
    for (const auto tag : tags) {
        auto value = extract_xml_value(xml, tag);
        if (!value.empty()) {
            return value;
        }
    }
    return {};
}

static Result<std::string> extract_xml_payload_loose(std::span<const std::byte> data)
{
    std::string body;
    body.reserve(data.size());
    for (const auto b : data) {
        body.push_back(static_cast<char>(b));
    }

    const auto xml_start = body.find("<?xml");
    if (xml_start == std::string::npos) {
        return make_error<std::string>(ErrorCode::XmlParseFailed,
                                       "xml payload tidak ditemukan");
    }

    auto xml = body.substr(xml_start);
    const auto zero = xml.find('\0');
    if (zero != std::string::npos) {
        xml.resize(zero);
    }
    return make_ok(std::move(xml));
}

[[nodiscard]] Result<SadpDeviceInfo>
PacketParser::parse_inquiry_reply(std::span<const std::byte> data)
{
    if (data.size() < SADP_HEADER_SIZE) {
        return make_error<SadpDeviceInfo>(ErrorCode::InvalidPacket,
                                          "data terlalu pendek");
    }

    // Beberapa firmware mengirim variasi command di response discovery.
    // Terima InquiryReply (0x02) dan Inquiry (0x01).
    const auto cmd = static_cast<std::uint8_t>(data[OFFSET_COMMAND]);
    if (cmd != static_cast<std::uint8_t>(SadpCommand::InquiryReply) &&
        cmd != static_cast<std::uint8_t>(SadpCommand::Inquiry)) {
        return make_error<SadpDeviceInfo>(ErrorCode::UnexpectedResponse,
            std::format("unexpected SADP command 0x{:02X}", cmd));
    }

    auto xml_result = extract_xml_payload(data);
    if (!xml_result || !verify_checksum(data)) {
        auto loose_xml = extract_xml_payload_loose(data);
        if (!loose_xml) {
            if (!xml_result) {
                return std::unexpected(xml_result.error());
            }
            return make_error<SadpDeviceInfo>(ErrorCode::InvalidChecksum);
        }
        xml_result = std::move(loose_xml);
    }

    const auto& xml = xml_result.value();

    // Sebagian perangkat menaruh MAC di src field (4-9), sebagian lain di dst (10-15).
    auto mac = parse_mac_bytes(data, OFFSET_SRC_MAC);
    if (!is_valid_mac(mac.get()) || mac.get() == "00:00:00:00:00:00") {
        mac = parse_mac_bytes(data, OFFSET_DST_MAC);
    }

    // Parse field dari XML payload
    SadpDeviceInfo info;
    info.mac_address      = mac;
    info.ip_address       = IpAddress{extract_xml_value_any(xml, {"IPv4Address", "ipv4Address", "ipAddress"})};
    info.subnet_mask      = IpAddress{extract_xml_value_any(xml, {"subnetMask", "subNetMask"})};
    info.gateway          = IpAddress{extract_xml_value_any(xml, {"defaultGateway", "gateway"})};
    info.serial_number    = SerialNumber{extract_xml_value_any(xml, {"serialNumber", "deviceID"})};
    info.firmware_version = FirmwareVersion{extract_xml_value_any(xml, {"firmwareVersion", "firmwareReleasedDate"})};
    info.model            = extract_xml_value_any(xml, {"model", "deviceModel"});
    info.device_type      = extract_xml_value_any(xml, {"deviceType", "type"});

    const auto sdk_str  = extract_xml_value_any(xml, {"sdkPort", "SDKPort"});
    const auto http_str = extract_xml_value_any(xml, {"httpPort", "HTTPPort"});
    const auto dhcp_str = extract_xml_value_any(xml, {"addressingType", "dhcpEnable"});
    const auto status   = extract_xml_value_any(xml, {"Activated", "activated", "activationStatus"});

    if (!sdk_str.empty()) {
        info.sdk_port = Port{static_cast<std::uint16_t>(std::stoul(sdk_str))};
    } else {
        info.sdk_port = ports::SDK_DEFAULT;
    }

    if (!http_str.empty()) {
        info.http_port = Port{static_cast<std::uint16_t>(std::stoul(http_str))};
    } else {
        info.http_port = ports::HTTP_DEFAULT;
    }

    info.dhcp_enabled = (dhcp_str == "dynamic" || dhcp_str == "true" || dhcp_str == "1");
    info.is_inactive  = (status == "false" || status.empty());

    // Validasi field wajib
    if (info.ip_address.get().empty()) {
        return make_error<SadpDeviceInfo>(ErrorCode::XmlParseFailed,
                                          "IPv4Address tidak ditemukan");
    }

    return make_ok(std::move(info));
}

[[nodiscard]] Result<bool>
PacketParser::parse_activate_reply(std::span<const std::byte> data)
{
    if (data.size() < SADP_HEADER_SIZE) {
        return make_error<bool>(ErrorCode::InvalidPacket);
    }

    const auto cmd = static_cast<std::uint8_t>(data[OFFSET_COMMAND]);
    if (cmd != static_cast<std::uint8_t>(SadpCommand::ActivateReply)) {
        return make_error<bool>(ErrorCode::UnexpectedResponse,
            std::format("expected ActivateReply (0x04), got 0x{:02X}", cmd));
    }

    if (!verify_checksum(data)) {
        return make_error<bool>(ErrorCode::InvalidChecksum);
    }

    auto xml_result = extract_xml_payload(data);
    if (!xml_result) return std::unexpected(xml_result.error());

    const auto status = extract_xml_value(xml_result.value(), "statusCode");
    if (status == "200") return make_ok(true);

    if (status == "401") {
        return make_error<bool>(ErrorCode::AuthenticationFailed);
    }

    return make_error<bool>(ErrorCode::UnexpectedResponse,
                             "statusCode: " + status);
}

[[nodiscard]] Result<bool>
PacketParser::parse_modify_reply(std::span<const std::byte> data)
{
    if (data.size() < SADP_HEADER_SIZE) {
        return make_error<bool>(ErrorCode::InvalidPacket);
    }

    const auto cmd = static_cast<std::uint8_t>(data[OFFSET_COMMAND]);
    if (cmd != static_cast<std::uint8_t>(SadpCommand::ModifyReply)) {
        return make_error<bool>(ErrorCode::UnexpectedResponse,
            std::format("expected ModifyReply (0x06), got 0x{:02X}", cmd));
    }

    if (!verify_checksum(data)) {
        return make_error<bool>(ErrorCode::InvalidChecksum);
    }

    auto xml_result = extract_xml_payload(data);
    if (!xml_result) return std::unexpected(xml_result.error());

    const auto status = extract_xml_value(xml_result.value(), "statusCode");
    if (status == "200") return make_ok(true);

    if (status == "401") {
        return make_error<bool>(ErrorCode::AuthenticationFailed);
    }

    return make_error<bool>(ErrorCode::UnexpectedResponse,
                             "statusCode: " + status);
}

std::string PacketParser::debug_summary(std::span<const std::byte> data)
{
    std::ostringstream ss;
    ss << "len=" << data.size();
    if (data.size() < SADP_HEADER_SIZE) {
        ss << " short_packet";
        return ss.str();
    }

    const auto version = static_cast<std::uint8_t>(data[OFFSET_VERSION]);
    const auto cmd = static_cast<std::uint8_t>(data[OFFSET_COMMAND]);
    const auto seq = read_u16_be(data, OFFSET_SEQUENCE);
    const auto payload_len = read_u16_be(data, OFFSET_PAYLOAD_LEN);
    const auto checksum_ok = verify_checksum(data);

    ss << std::format(" ver=0x{:02X} cmd=0x{:02X} seq={} payload_len={} checksum_ok={}",
                      version, cmd, seq, payload_len, checksum_ok ? "yes" : "no");

    auto xml = extract_xml_payload(data);
    if (!xml) {
        xml = extract_xml_payload_loose(data);
    }
    if (xml) {
        const auto& x = xml.value();
        const auto preview_len = std::min<std::size_t>(x.size(), 220);
        ss << " xml_preview=\"";
        ss << x.substr(0, preview_len);
        if (x.size() > preview_len) {
            ss << "...";
        }
        ss << "\"";
    } else {
        ss << " xml_preview=<none>";
    }
    return ss.str();
}

} // namespace hiksadp::protocol
