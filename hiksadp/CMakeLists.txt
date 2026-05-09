cmake_minimum_required(VERSION 3.26)

project(
  HikSADP
  VERSION 1.0.0
  DESCRIPTION "Hikvision SADP Tool - Native Linux"
  LANGUAGES CXX
)

# ── C++23 standard, mandatory ──────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Compile options (strict warnings = early bug detection) ────────────────
add_compile_options(
  -Wall
  -Wextra
  -Wpedantic
  -Wconversion
  -Wshadow
  -Wnull-dereference
  -Wdouble-promotion
  -Wformat=2
  -Werror=return-type
  -Werror=uninitialized
)

# ── Export compile_commands.json for clangd / IDE support ─────────────────
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Find Qt6 ──────────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS Core Network Widgets)
qt_standard_project_setup()

option(HIKSADP_BUILD_TESTS "Build unit tests" ON)

# ── Subdirectories ────────────────────────────────────────────────────────
add_subdirectory(src)
if(HIKSADP_BUILD_TESTS)
  add_subdirectory(tests)
endif()
