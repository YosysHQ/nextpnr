add_library(nextpnr_route INTERFACE)

target_include_directories(nextpnr_route INTERFACE .)

target_sources(nextpnr_route PUBLIC
    router1.cc
    router1.h
    router2.cc
    router2.h
)
