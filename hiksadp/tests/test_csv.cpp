#include <catch2/catch_test_macros.hpp>

#include "core/csv.hpp"

using namespace hiksadp;

TEST_CASE("csv: plain text no quotes", "[csv]") {
    REQUIRE(escape_csv_field("abc123") == "abc123");
}

TEST_CASE("csv: comma quoted", "[csv]") {
    REQUIRE(escape_csv_field("a,b") == "\"a,b\"");
}

TEST_CASE("csv: quote doubled", "[csv]") {
    REQUIRE(escape_csv_field("a\"b") == "\"a\"\"b\"");
}

TEST_CASE("csv: newline quoted", "[csv]") {
    REQUIRE(escape_csv_field("a\nb") == "\"a\nb\"");
}

