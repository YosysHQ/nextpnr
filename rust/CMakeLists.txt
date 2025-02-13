add_library(nextpnr_rust INTERFACE)

target_include_directories(nextpnr_rust INTERFACE .)

target_sources(nextpnr_rust INTERFACE rust.h)
if (BUILD_RUST)
    target_sources(nextpnr_rust INTERFACE rust.cc)
endif()
