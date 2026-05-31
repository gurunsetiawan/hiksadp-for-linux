cmake_minimum_required(VERSION 3.26)

project(
  HikSADP
  VERSION 1.0.0
  DESCRIPTION "Hikvision SADP Tool - Native Linux"
  LANGUAGES CXX
)

include(CTest)
enable_testing()
include(GNUInstallDirs)

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

option(HIKSADP_ENABLE_PACKAGING "Enable CPack packaging" ON)
if(HIKSADP_ENABLE_PACKAGING)
  set(HIKSADP_PACKAGE_VERSION "${PROJECT_VERSION}" CACHE STRING "Package version used by CPack")
  set(CPACK_PACKAGE_NAME "hiksadp")
  set(CPACK_PACKAGE_VENDOR "gurunsetiawan")
  set(CPACK_PACKAGE_CONTACT "maintainer@local")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "HikSADP Linux - Hikvision SADP alternative")
  set(CPACK_PACKAGE_VERSION "${HIKSADP_PACKAGE_VERSION}")
  set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}")
  set(CPACK_GENERATOR "TGZ;DEB")
  set(CPACK_DEBIAN_PACKAGE_SECTION "net")
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  include(CPack)
endif()
