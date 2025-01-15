add_library(nextpnr_json INTERFACE)

target_include_directories(nextpnr_json INTERFACE .)

target_sources(nextpnr_json PUBLIC
    jsonwrite.cc
    jsonwrite.h
)
