add_library(nextpnr_kernel INTERFACE)

target_include_directories(nextpnr_kernel INTERFACE .)

target_sources(nextpnr_kernel PUBLIC
    arch_api.h
    archcheck.cc
    arch_pybindings_shared.h
    array2d.h
    base_arch.h
    base_clusterinfo.h
    basectx.cc
    basectx.h
    bits.cc
    bits.h
    chain_utils.h
    command.cc
    command.h
    constraints.h
    constraints.impl.h
    context.cc
    context.h
    design_utils.cc
    design_utils.h
    deterministic_rng.h
    dynamic_bitarray.h
    embed.cc
    embed.h
    exclusive_state_groups.h
    exclusive_state_groups.impl.h
    handle_error.cc
    hashlib.h
    idstring.cc
    idstring.h
    idstringlist.cc
    idstringlist.h
    indexed_store.h
    log.cc
    log.h
    nextpnr_assertions.cc
    nextpnr_assertions.h
    nextpnr_base_types.h
    nextpnr.cc
    nextpnr.h
    nextpnr_namespaces.cc
    nextpnr_namespaces.h
    nextpnr_types.cc
    nextpnr_types.h
    property.cc
    property.h
    pybindings.cc
    pybindings.h
    pycontainers.h
    pywrappers.h
    relptr.h
    report.cc
    scope_lock.h
    sdc.cc
    sdf.cc
    sso_array.h
    str_ring_buffer.cc
    str_ring_buffer.h
    svg.cc
    timing.cc
    timing.h
    timing_log.cc
    util.h
)

target_link_libraries(nextpnr_kernel INTERFACE
    nextpnr_version
    nextpnr_frontend
    nextpnr_json
    nextpnr_rust
    json11
)

if (BUILD_PYTHON)
    target_include_directories(nextpnr_kernel INTERFACE
        ${Python3_INCLUDE_DIRS}
    )

    target_link_libraries(nextpnr_kernel INTERFACE
        pybind11_headers
    )
endif()
