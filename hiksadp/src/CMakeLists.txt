# ── Core library (pure C++23, no Qt dependency) ───────────────────────────
add_library(hiksadp_core STATIC
  core/types.hpp          # strong typedefs & domain primitives
  core/result.hpp         # Result<T,E> alias atas std::expected
  core/device.hpp         # Device struct & DeviceState variant
  core/csv.hpp
  core/csv.cpp
  core/logger.hpp
  core/logger.cpp
  core/device.cpp
)

target_include_directories(hiksadp_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(hiksadp_core PUBLIC cxx_std_23)

# ── Protocol library (UDP discovery + ISAPI, depends on Qt6::Network) ─────
add_library(hiksadp_protocol STATIC
  protocol/sadp_packet.hpp
  protocol/sadp_packet.cpp
  protocol/sadp_discovery.hpp
  protocol/sadp_discovery.cpp
)

target_include_directories(hiksadp_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hiksadp_protocol
  PUBLIC hiksadp_core
  PRIVATE Qt6::Network Qt6::Core
)

add_library(hiksadp_management STATIC
  management/isapi_client.hpp
  management/isapi_client.cpp
  management/password_reset_service.hpp
  management/password_reset_service.cpp
  management/device_manager.hpp
  management/device_manager.cpp
)

target_include_directories(hiksadp_management PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hiksadp_management
  PUBLIC hiksadp_core
  PRIVATE Qt6::Network Qt6::Core
)

add_library(hiksadp_ui STATIC
  ui/device_table.hpp
  ui/device_table.cpp
  ui/main_window.hpp
  ui/main_window.cpp
)

target_include_directories(hiksadp_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hiksadp_ui
  PUBLIC hiksadp_core hiksadp_management hiksadp_protocol
  PRIVATE Qt6::Widgets Qt6::Core Qt6::Network
)

# ── CLI executable (Phase 1) ───────────────────────────────────────────────
add_executable(hiksadp_cli
  main_cli.cpp
)

target_link_libraries(hiksadp_cli
  PRIVATE hiksadp_protocol Qt6::Core Qt6::Network
)

add_executable(hiksadp_gui
  main_gui.cpp
)

target_link_libraries(hiksadp_gui
  PRIVATE hiksadp_ui hiksadp_management hiksadp_protocol Qt6::Widgets Qt6::Core Qt6::Network
)
