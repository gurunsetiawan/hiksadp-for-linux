find_package(Catch2 3 QUIET)
if(NOT Catch2_FOUND)
  message(WARNING "Catch2 v3 not found. Skipping tests target.")
  return()
endif()

add_executable(hiksadp_tests
  test_types.cpp
  test_packet.cpp
)

target_link_libraries(hiksadp_tests
  PRIVATE
    hiksadp_core
    hiksadp_protocol
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Network
)

include(CTest)
include(Catch)
catch_discover_tests(hiksadp_tests)
