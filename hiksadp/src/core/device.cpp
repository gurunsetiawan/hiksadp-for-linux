#include "device.hpp"
#include "types.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <string_view>

namespace hiksadp {

[[nodiscard]] bool is_valid_ip(std::string_view ip) noexcept {
    // Format: X.X.X.X di mana X adalah 0-255
    static const std::regex ipv4_regex{
        R"(^((25[0-5]|2[0-4]\d|[01]?\d\d?)\.){3}(25[0-5]|2[0-4]\d|[01]?\d\d?)$)"
    };
    return std::regex_match(std::string{ip}, ipv4_regex);
}

[[nodiscard]] bool is_valid_mac(std::string_view mac) noexcept {
    // Format: XX:XX:XX:XX:XX:XX atau XX-XX-XX-XX-XX-XX
    static const std::regex mac_regex{
        R"(^([0-9A-Fa-f]{2}[:\-]){5}[0-9A-Fa-f]{2}$)"
    };
    return std::regex_match(std::string{mac}, mac_regex);
}

[[nodiscard]] bool is_strong_password(std::string_view password) noexcept {
    // Requirement Hikvision:
    // - minimal 8 karakter, maksimal 16
    // - minimal 3 dari 4 kategori: huruf besar, huruf kecil, angka, simbol
    if (password.size() < 8 || password.size() > 16) {
        return false;
    }

    int categories = 0;

    bool has_upper  = std::ranges::any_of(password, [](char c){ return std::isupper(static_cast<unsigned char>(c)); });
    bool has_lower  = std::ranges::any_of(password, [](char c){ return std::islower(static_cast<unsigned char>(c)); });
    bool has_digit  = std::ranges::any_of(password, [](char c){ return std::isdigit(static_cast<unsigned char>(c)); });
    bool has_symbol = std::ranges::any_of(password, [](char c){
        return std::ispunct(static_cast<unsigned char>(c));
    });

    if (has_upper)  ++categories;
    if (has_lower)  ++categories;
    if (has_digit)  ++categories;
    if (has_symbol) ++categories;

    return categories >= 3;
}

} // namespace hiksadp
