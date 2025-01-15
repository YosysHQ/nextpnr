add_library(nextpnr_place INTERFACE)

target_include_directories(nextpnr_place INTERFACE .)

target_sources(nextpnr_place PUBLIC
    detail_place_cfg.h
    detail_place_core.cc
    detail_place_core.h
    fast_bels.h
    parallel_refine.cc
    parallel_refine.h
    place_common.cc
    place_common.h
    placer1.cc
    placer1.h
    placer_heap.cc
    placer_heap.h
    placer_static.cc
    placer_static.h
    static_util.h
    timing_opt.cc
    timing_opt.h
)
