set(SOURCES
    arch.cc
    archdefs.h
    arch.h
    arch_pybindings.cc
    arch_pybindings.h
    cells.cc
    cells.h
    pack.cc
    viaduct_api.cc
    viaduct_api.h
    viaduct_constids.h
    viaduct_helpers.cc
    viaduct_helpers.h
    viaduct/example/constids.inc
    viaduct/example/example.cc
    viaduct/fabulous/constids.inc
    viaduct/fabulous/fab_cfg.h
    viaduct/fabulous/fab_defs.h
    viaduct/fabulous/fabric_parsing.h
    viaduct/fabulous/fabulous.cc
    viaduct/fabulous/fasm.cc
    viaduct/fabulous/fasm.h
    viaduct/fabulous/pack.cc
    viaduct/fabulous/pack.h
    viaduct/fabulous/validity_check.cc
    viaduct/fabulous/validity_check.h
    viaduct/fabulous/pcf.cc
    viaduct/fabulous/pcf.h
)

add_nextpnr_architecture(${family}
    CORE_SOURCES ${SOURCES}
    MAIN_SOURCE  main.cc
)

target_sources(nextpnr-${family}-chipdb PUBLIC chipdb.cc)
