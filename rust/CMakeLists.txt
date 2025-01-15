add_library(nextpnr_rust INTERFACE)

target_include_directories(nextpnr_rust INTERFACE .)

if (USE_RUST)
    target_sources(nextpnr_rust PUBLIC
        rust.cc
        rust.h
    )
endif()
