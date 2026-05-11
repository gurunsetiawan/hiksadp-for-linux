#pragma once

#include "core/device.hpp"
#include "core/result.hpp"

#include <string>

namespace hiksadp {

struct PasswordResetResponse {
    std::string serial;
    std::string timestamp;
    std::string reset_code;
};

class PasswordResetService {
public:
    [[nodiscard]] Result<std::string>
    build_request_xml(const Device& device, const std::string& iso_timestamp) const;

    [[nodiscard]] Result<PasswordResetResponse>
    parse_response_xml(const std::string& xml) const;
};

} // namespace hiksadp

