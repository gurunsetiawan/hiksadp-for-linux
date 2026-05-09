#include <catch2/catch_test_macros.hpp>
#include "protocol/sadp_packet.hpp"

using namespace hiksadp;
using namespace hiksadp::protocol;

// ── PacketBuilder tests ────────────────────────────────────────────────────

TEST_CASE("build_inquiry menghasilkan packet dengan ukuran benar", "[packet]") {
    auto result = PacketBuilder::build_inquiry(0);
    REQUIRE(result.has_value());
    // Inquiry tidak punya payload, hanya header 20 bytes
    REQUIRE(result.value().size() == SADP_HEADER_SIZE);
}

TEST_CASE("build_inquiry menggunakan version dan command yang benar", "[packet]") {
    auto result = PacketBuilder::build_inquiry(42);
    REQUIRE(result.has_value());

    const auto& packet = result.value();
    // Version di offset 0
    REQUIRE(static_cast<uint8_t>(packet[0]) == SADP_VERSION);
    // Command Inquiry (0x01) di offset 1
    REQUIRE(static_cast<uint8_t>(packet[1]) == 0x01);
    // Sequence number 42 di offset 2-3 (big-endian)
    REQUIRE(static_cast<uint8_t>(packet[2]) == 0x00);
    REQUIRE(static_cast<uint8_t>(packet[3]) == 42);
}

TEST_CASE("build_activate gagal dengan MAC tidak valid", "[packet]") {
    MacAddress bad_mac{"not-a-mac"};
    Password   pwd{"Admin@1234"};

    auto result = PacketBuilder::build_activate(bad_mac, pwd);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == ErrorCode::InvalidMacAddress);
}

TEST_CASE("build_activate gagal dengan password lemah", "[packet]") {
    MacAddress mac{"AA:BB:CC:DD:EE:FF"};
    Password   weak_pwd{"12345678"};

    auto result = PacketBuilder::build_activate(mac, weak_pwd);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == ErrorCode::WeakPassword);
}

TEST_CASE("build_activate berhasil dengan input valid", "[packet]") {
    MacAddress mac{"AA:BB:CC:DD:EE:FF"};
    Password   pwd{"Admin@1234"};

    auto result = PacketBuilder::build_activate(mac, pwd);
    REQUIRE(result.has_value());

    const auto& packet = result.value();
    REQUIRE(packet.size() > SADP_HEADER_SIZE); // ada XML payload
    // Command Activate (0x03)
    REQUIRE(static_cast<uint8_t>(packet[1]) == 0x03);
}

TEST_CASE("build_modify_network gagal dengan IP tidak valid", "[packet]") {
    MacAddress mac{"AA:BB:CC:DD:EE:FF"};
    Password   pwd{"Admin@1234"};

    auto result = PacketBuilder::build_modify_network(
        mac, pwd,
        IpAddress{"999.999.999.999"},  // IP tidak valid
        IpAddress{"255.255.255.0"},
        IpAddress{"192.168.1.1"},
        ports::SDK_DEFAULT,
        ports::HTTP_DEFAULT,
        false
    );

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == ErrorCode::InvalidIpAddress);
}

TEST_CASE("build_modify_network berhasil dengan input valid", "[packet]") {
    MacAddress mac{"AA:BB:CC:DD:EE:FF"};
    Password   pwd{"Admin@1234"};

    auto result = PacketBuilder::build_modify_network(
        mac, pwd,
        IpAddress{"192.168.1.100"},
        IpAddress{"255.255.255.0"},
        IpAddress{"192.168.1.1"},
        ports::SDK_DEFAULT,
        ports::HTTP_DEFAULT,
        false
    );

    REQUIRE(result.has_value());
    const auto& packet = result.value();
    REQUIRE(packet.size() > SADP_HEADER_SIZE);
    // Command ModifyNet (0x05)
    REQUIRE(static_cast<uint8_t>(packet[1]) == 0x05);
}

// ── PacketParser tests ─────────────────────────────────────────────────────

TEST_CASE("parse_inquiry_reply gagal dengan data kosong", "[parser]") {
    std::vector<std::byte> empty{};
    auto result = PacketParser::parse_inquiry_reply(empty);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == ErrorCode::InvalidPacket);
}

TEST_CASE("parse_inquiry_reply gagal dengan command type salah", "[parser]") {
    // Buat packet dengan command 0x01 (Inquiry) bukan 0x02 (InquiryReply)
    auto inquiry = PacketBuilder::build_inquiry(0);
    REQUIRE(inquiry.has_value());

    auto result = PacketParser::parse_inquiry_reply(inquiry.value());
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == ErrorCode::UnexpectedResponse);
}

TEST_CASE("parse_inquiry_reply berhasil mem-parse XML response yang valid", "[parser]") {
    // Simulasi packet InquiryReply yang valid dengan XML payload
    // Format: header 20 bytes + XML payload
    const std::string xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<DeviceInfo>)"
        R"(<IPv4Address>192.168.1.64</IPv4Address>)"
        R"(<subnetMask>255.255.255.0</subnetMask>)"
        R"(<defaultGateway>192.168.1.1</defaultGateway>)"
        R"(<serialNumber>DS-ABC123456789</serialNumber>)"
        R"(<firmwareVersion>V5.7.3_build230615</firmwareVersion>)"
        R"(<model>DS-2CD2143G2-I</model>)"
        R"(<deviceType>IPCamera</deviceType>)"
        R"(<httpPort>80</httpPort>)"
        R"(<sdkPort>8000</sdkPort>)"
        R"(<addressingType>static</addressingType>)"
        R"(<Activated>true</Activated>)"
        R"(</DeviceInfo>)";

    const auto payload_len = static_cast<std::uint16_t>(xml.size());

    std::vector<std::byte> packet(SADP_HEADER_SIZE + xml.size(), std::byte{0x00});

    // Set version
    packet[0] = static_cast<std::byte>(SADP_VERSION);
    // Set command: InquiryReply = 0x02
    packet[1] = std::byte{0x02};
    // Set payload length (big-endian) di offset 16
    packet[16] = static_cast<std::byte>((payload_len >> 8) & 0xFF);
    packet[17] = static_cast<std::byte>(payload_len & 0xFF);
    // Set destination MAC (offset 10-15) — ini yang dipakai sebagai device MAC
    packet[10] = std::byte{0xAA};
    packet[11] = std::byte{0xBB};
    packet[12] = std::byte{0xCC};
    packet[13] = std::byte{0xDD};
    packet[14] = std::byte{0xEE};
    packet[15] = std::byte{0xFF};

    // Tulis XML payload
    for (std::size_t i = 0; i < xml.size(); ++i) {
        packet[SADP_HEADER_SIZE + i] = static_cast<std::byte>(xml[i]);
    }

    // Hitung checksum dan set di offset 18
    std::vector<std::byte> for_cs;
    for (std::size_t i = 0; i < packet.size(); ++i) {
        if (i != 18) for_cs.push_back(packet[i]);
    }
    packet[18] = static_cast<std::byte>(
        PacketBuilder::calculate_checksum(for_cs));

    auto result = PacketParser::parse_inquiry_reply(packet);
    REQUIRE(result.has_value());

    const auto& info = result.value();
    REQUIRE(info.ip_address.get()       == "192.168.1.64");
    REQUIRE(info.subnet_mask.get()      == "255.255.255.0");
    REQUIRE(info.gateway.get()          == "192.168.1.1");
    REQUIRE(info.serial_number.get()    == "DS-ABC123456789");
    REQUIRE(info.firmware_version.get() == "V5.7.3_build230615");
    REQUIRE(info.model                  == "DS-2CD2143G2-I");
    REQUIRE(info.device_type            == "IPCamera");
    REQUIRE(info.http_port.get()        == 80);
    REQUIRE(info.sdk_port.get()         == 8000);
    REQUIRE_FALSE(info.dhcp_enabled);
    REQUIRE_FALSE(info.is_inactive);
    REQUIRE(info.mac_address.get()      == "AA:BB:CC:DD:EE:FF");
}
