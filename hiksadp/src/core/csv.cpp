#include "core/csv.hpp"

namespace hiksadp {

std::string escape_csv_field(std::string_view in)
{
    std::string out;
    out.reserve(in.size() + 2);

    bool need_quotes = false;
    for (const char ch : in) {
        if (ch == ',' || ch == '"' || ch == '\n' || ch == '\r') {
            need_quotes = true;
        }
        if (ch == '"') {
            out.push_back('"');
            out.push_back('"');
        } else {
            out.push_back(ch);
        }
    }

    if (!need_quotes) return out;
    return "\"" + out + "\"";
}

} // namespace hiksadp

