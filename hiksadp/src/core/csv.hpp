#pragma once

#include <string>
#include <string_view>

namespace hiksadp {

[[nodiscard]] std::string escape_csv_field(std::string_view in);

} // namespace hiksadp

