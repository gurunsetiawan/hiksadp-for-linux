include(CTest)

add_executable(hiksadp_smoke_tests
  test_smoke.cpp
)

target_link_libraries(hiksadp_smoke_tests
  PRIVATE
    hiksadp_core
    hiksadp_protocol
    hiksadp_management
    Qt6::Core
    Qt6::Network
)

add_test(NAME hiksadp_smoke_tests COMMAND hiksadp_smoke_tests)

find_package(Catch2 3 QUIET)
if(Catch2_FOUND)
  add_executable(hiksadp_tests
    test_types.cpp
    test_packet.cpp
    test_csv.cpp
  )

  target_link_libraries(hiksadp_tests
    PRIVATE
      hiksadp_core
      hiksadp_protocol
      Catch2::Catch2WithMain
      Qt6::Core
      Qt6::Network
  )

  include(Catch)
  catch_discover_tests(hiksadp_tests)
else()
  message(STATUS "Catch2 v3 not found. Building smoke tests only.")
endif()
