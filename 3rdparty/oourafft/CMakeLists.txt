add_library(oourafft INTERFACE)

target_include_directories(oourafft INTERFACE .)

target_sources(oourafft PUBLIC
    fftsg.cc
    fftsg.h
    fftsg2d.cc
)
