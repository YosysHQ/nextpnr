add_library(nextpnr_json INTERFACE)

target_include_directories(nextpnr_json INTERFACE .)

target_sources(nextpnr_json INTERFACE
    jsonwrite.cc
    jsonwrite.h
)
