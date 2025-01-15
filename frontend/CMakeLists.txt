add_library(nextpnr_frontend INTERFACE)

target_include_directories(nextpnr_frontend INTERFACE .)

target_sources(nextpnr_frontend PUBLIC
    frontend_base.h
    json_frontend.cc
    json_frontend.h
)
