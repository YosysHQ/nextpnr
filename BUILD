# Copyright 2020 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

""" BUILD file for nextpnr. Does currently not include the GUI. """

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("@rules_python//python:defs.bzl", "py_binary")
load("@pybind11_bazel//:build_defs.bzl", "pybind_library")

package(
    features = [
        "-use_header_modules",  # For exceptions.
    ],
)

licenses(["notice"])

exports_files(["LICENSE"])

DEFINES = [
    "NO_GUI",
    "NO_RUST",
]

FAMILIES = [
    "generic",
    "ice40",
    "ecp5",
]

BBASM_ENDIAN_FLAG = "--le"

genrule(
    name = "version_h",
    srcs = ["common/version.h.in"],
    outs = ["common/version.h"],
    cmd = "cat $(SRCS) | sed 's/@CURRENT_GIT_VERSION@/0.8.0-18c7b40/g' > $@",
)

cc_binary(
    name = "bbasm",
    srcs = ["bba/main.cc"],
    deps = [
        "@boost.filesystem",
        "@boost.program_options",
    ],
)

COMMON_KERNEL_SRCS = [
    "common/kernel/sdc.cc",
    "common/kernel/timing.cc",
    "common/kernel/bits.cc",
    "common/kernel/command.cc",
    "common/kernel/design_utils.cc",
    "common/kernel/log.cc",
    "common/kernel/context.cc",
    "common/kernel/nextpnr_types.cc",
    "common/kernel/nextpnr_assertions.cc",
    "common/kernel/pybindings.cc",
    "common/kernel/nextpnr.cc",
    "common/kernel/basectx.cc",
    "common/kernel/embed.cc",
    "common/kernel/report.cc",
    "common/kernel/sdf.cc",
    "common/kernel/idstring.cc",
    "common/kernel/property.cc",
    "common/kernel/str_ring_buffer.cc",
    "common/kernel/nextpnr_namespaces.cc",
    "common/kernel/idstringlist.cc",
    "common/kernel/timing_log.cc",
    "common/kernel/svg.cc",
    "common/kernel/handle_error.cc",
    "common/kernel/archcheck.cc",
]

COMMON_KERNEL_HDRS = [
    "common/kernel/basectx.h",
    "common/kernel/pybindings.h",
    "common/kernel/nextpnr_base_types.h",
    "common/kernel/sso_array.h",
    "common/kernel/nextpnr_types.h",
    "common/kernel/base_clusterinfo.h",
    "common/kernel/base_arch.h",
    "common/kernel/nextpnr_namespaces.h",
    "common/kernel/bits.h",
    "common/kernel/log.h",
    "common/kernel/scope_lock.h",
    "common/kernel/idstring.h",
    "common/kernel/deterministic_rng.h",
    "common/kernel/nextpnr_assertions.h",
    "common/kernel/exclusive_state_groups.impl.h",
    "common/kernel/relptr.h",
    "common/kernel/embed.h",
    "common/kernel/nextpnr.h",
    "common/kernel/constraints.h",
    "common/kernel/indexed_store.h",
    "common/kernel/arch_pybindings_shared.h",
    "common/kernel/constraints.impl.h",
    "common/kernel/chain_utils.h",
    "common/kernel/hashlib.h",
    "common/kernel/pycontainers.h",
    "common/kernel/dynamic_bitarray.h",
    "common/kernel/context.h",
    "common/kernel/property.h",
    "common/kernel/design_utils.h",
    "common/kernel/idstringlist.h",
    "common/kernel/pywrappers.h",
    "common/kernel/timing.h",
    "common/kernel/command.h",
    "common/kernel/arch_api.h",
    "common/kernel/array2d.h",
    "common/kernel/str_ring_buffer.h",
    "common/kernel/exclusive_state_groups.h",
    "common/kernel/util.h",
]

COMMON_ROUTE_SRCS = [
    "common/route/router2.cc",
    "common/route/router1.cc",
]

COMMON_ROUTE_HDRS = [
    "common/route/router2.h",
    "common/route/router1.h",
]

COMMON_PLACE_SRCS = [
    "common/place/placer1.cc",
    "common/place/place_common.cc",
    "common/place/detail_place_core.cc",
    "common/place/placer_heap.cc",
    "common/place/placer_static.cc",
    "common/place/parallel_refine.cc",
    "common/place/timing_opt.cc",
]

COMMON_PLACE_HDRS = [
    "common/place/detail_place_core.h",
    "common/place/parallel_refine.h",
    "common/place/placer_static.h",
    "common/place/timing_opt.h",
    "common/place/fast_bels.h",
    "common/place/placer1.h",
    "common/place/placer_heap.h",
    "common/place/detail_place_cfg.h",
    "common/place/place_common.h",
    "common/place/static_util.h",
]

JSON_SRCS = [
    "json/jsonwrite.cc",
]

JSON_HDRS = [
    "json/jsonwrite.h",
]

FRONTEND_SRCS = [
    "frontend/json_frontend.cc",
]

FRONTEND_HDRS = [
    "frontend/frontend_base.h",
    "frontend/json_frontend.h",
]

FFTSG_SRCS = [
    "3rdparty/oourafft/fftsg.cc",
    "3rdparty/oourafft/fftsg2d.cc",
]

FFTSG_HDRS = [
    "3rdparty/oourafft/fftsg.h",
]

CORE_SRCS = COMMON_KERNEL_SRCS \
    + COMMON_ROUTE_SRCS \
    + COMMON_PLACE_SRCS \
    + JSON_SRCS \
    + FRONTEND_SRCS \
    + FFTSG_SRCS

CORE_HDRS = COMMON_KERNEL_HDRS \
    + COMMON_ROUTE_HDRS \
    + COMMON_PLACE_HDRS \
    + JSON_HDRS \
    + FRONTEND_HDRS \
    + FFTSG_HDRS \
    + [
        "rust/rust.h",
        "common/version.h",
    ]

ICE40_DEFINES = [
    "NEXTPNR_NAMESPACE=nextpnr_ice40",
    "ARCH_ICE40",
    "ARCHNAME=ice40",
]

ICE40_DEVICES = [
    "384",
    "1k",
    "5k",
    "u4k",
    "8k",
]

ICE40_CHIPDB_OPTS = {
    "384": ' '.join([
        "--slow $(location @icestorm//:icefuzz/timings_lp384.txt)"
    ]),
    "1k":  ' '.join([
        "--fast $(location @icestorm//:icefuzz/timings_hx1k.txt)",
        "--slow $(location @icestorm//:icefuzz/timings_lp1k.txt)",
    ]),
    "5k":  ' '.join([
        "--slow $(location @icestorm//:icefuzz/timings_up5k.txt)"
    ]),
    "u4k": ' '.join([
        "--slow $(location @icestorm//:icefuzz/timings_u4k.txt)"
    ]),
    "8k":  ' '.join([
        "--fast $(location @icestorm//:icefuzz/timings_hx8k.txt)",
        " --slow $(location @icestorm//:icefuzz/timings_lp8k.txt)",
    ]),
}

py_binary(
    name = "ice40_chipdb_py",
    srcs = ["ice40/chipdb.py"],
    main = "ice40/chipdb.py",
    python_version = "PY3",
)

[genrule(
    name = "ice40_chipdb_%s_bba" % device,
    srcs = [
        "ice40/constids.inc",
        "ice40/gfx.h",
        "@icestorm//:chipdb-%s.txt" % device,
        "@icestorm//:icefuzz/timings_lp384.txt",
        "@icestorm//:icefuzz/timings_hx1k.txt",
        "@icestorm//:icefuzz/timings_lp1k.txt",
        "@icestorm//:icefuzz/timings_up5k.txt",
        "@icestorm//:icefuzz/timings_u4k.txt",
        "@icestorm//:icefuzz/timings_hx8k.txt",
        "@icestorm//:icefuzz/timings_lp8k.txt",
    ],
    outs = ["generated/ice40_chipdb_%s.bba" % device],
    cmd = ' '.join([
        "$(location :ice40_chipdb_py)",
        "-p $(location ice40/constids.inc)",
        "-g $(location ice40/gfx.h)",
        "%s $(location @icestorm//:chipdb-%s.txt) > $@",
    ]) % (
        ICE40_CHIPDB_OPTS[device],
        device,
    ),
    tools = [":ice40_chipdb_py"],
) for device in ICE40_DEVICES]

[genrule(
    name = "ice40_chipdb_%s_cc" % device,
    srcs = ["generated/ice40_chipdb_%s.bba" % device],
    outs = ["generated/ice40_chipdb_%s.cc" % device],
    cmd = "$(location :bbasm) --c %s $(SRCS) $@" % BBASM_ENDIAN_FLAG,
    tools = [":bbasm"],
) for device in ICE40_DEVICES]

ICE40_HDRS = [
    "ice40/archdefs.h",
    "ice40/arch.h",
    "ice40/constids.inc",
]

cc_library(
    name = "ice40_chipdb",
    srcs = [
        "generated/ice40_chipdb_%s.cc" % device
        for device in ICE40_DEVICES
    ] + COMMON_KERNEL_HDRS + ICE40_HDRS,
    defines = ICE40_DEFINES,
    includes = [
        "ice40",
        "common/kernel",
    ],
    deps = [
        "@boost.functional",
        "@boost.lexical_cast",
        "@boost.thread",
    ],
)

# The CLI binary target, one for each architecture.
pybind_library(
    name = "ice40",
    srcs = [
        "ice40/arch.cc",
        "ice40/archdefs.h",
        "ice40/arch.h",
        "ice40/arch_place.cc",
        "ice40/arch_pybindings.cc",
        "ice40/arch_pybindings.h",
        "ice40/bitstream.cc",
        "ice40/bitstream.h",
        "ice40/cells.cc",
        "ice40/cells.h",
        "ice40/chains.cc",
        "ice40/chains.h",
        "ice40/delay.cc",
        "ice40/gfx.cc",
        "ice40/gfx.h",
        "ice40/pack.cc",
        "ice40/pcf.cc",
        "ice40/pcf.h",
        "ice40/constids.inc",
    ] + CORE_SRCS + CORE_HDRS,
    cxxopts = [
        "-std=c++17",
        "-fexceptions",
        "-Wno-unused-variable",
        "-Wno-implicit-conversion-floating-point-to-bool",
        "-Wno-implicit-fallthrough",
    ],
    defines = DEFINES + ICE40_DEFINES,
    includes = [
        "common/kernel",
        "common/route",
        "common/place",
        "common",
        "json",
        "frontend",
        "3rdparty/oourafft",
        "rust",
    ],
    linkopts = ["-lpthread"],
    visibility = ["//visibility:public"],
    deps = [
        ":ice40_chipdb",
        "@eigen",
        "@json11",
        "@boost.filesystem",
        "@boost.algorithm",
        "@boost.functional",
        "@boost.lexical_cast",
        "@boost.thread",
        "@boost.iostreams",
        "@boost.program_options",
        "@boost.system",
    ],
)

cc_binary(
    name = "nextpnr-ice40",
    srcs = [
        "ice40/main.cc",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":ice40",
        "@rules_python//python/cc:current_py_cc_libs"
    ],
)

cc_test(
    name = "ice40_test",
    srcs =  glob([
        "ice40/tests/*.cc",
    ]),
    deps = [
        ":ice40",
        "@googletest//:gtest_main",
        "@rules_python//python/cc:current_py_cc_libs",
    ],
)

ECP5_DEFINES = [
    "NEXTPNR_NAMESPACE=nextpnr_ecp5",
    "ARCH_ECP5",
    "ARCHNAME=ecp5",
]

ECP5_DEVICES = [
    "25k",
    "45k",
    "85k",
]

py_binary(
    name = "trellis_import",
    srcs = ["ecp5/trellis_import.py"],
    python_version = "PY3",
    deps = [
        "@prjtrellis//:prjtrellis",
    ],
)

[genrule(
    name = "ecp5_chipdb_%s_bba" % device,
    srcs = [
        "ecp5/constids.inc",
        "ecp5/gfx.h",
        "@prjtrellis_db//:devices.json",
        "@prjtrellis_db//:files",
    ],
    outs = ["generated/ecp5_chipdb_%s.bba" % device],
    cmd = " ".join([
        "PRJTRELLIS_DB=$$(dirname '$(location @prjtrellis_db//:devices.json)')",
        "$(location :trellis_import)",
        "-p $(location ecp5/constids.inc)",
        "-g $(location ecp5/gfx.h) %s > $@"]) % device,
    tools = [":trellis_import"],
) for device in ECP5_DEVICES]

[genrule(
    name = "ecp5_chipdb_%s_cc" % device,
    srcs = ["generated/ecp5_chipdb_%s.bba" % device],
    outs = ["generated/ecp5_chipdb_%s.cc" % device],
    cmd = "$(location :bbasm) --c %s $(SRCS) $@" % BBASM_ENDIAN_FLAG,
    tools = [":bbasm"],
) for device in ECP5_DEVICES]

ECP5_HDRS = [
    "ecp5/arch.h",
    "ecp5/config.h",
    "ecp5/archdefs.h",
    "ecp5/globals.h",
    "ecp5/bitstream.h",
    "ecp5/dcu_bitstream.h",
    "ecp5/pio.h",
    "ecp5/cells.h",
    "ecp5/arch_pybindings.h",
    "ecp5/gfx.h",
    "ecp5/constids.inc",
]

cc_library(
    name = "ecp5_chipdb",
    srcs = [
        "generated/ecp5_chipdb_%s.cc" % device
        for device in ECP5_DEVICES
    ] + COMMON_KERNEL_HDRS + ECP5_HDRS,
    defines = ECP5_DEFINES,
    includes = [
        "ecp5",
        "common/kernel",
    ],
    deps = [
        "@boost.thread",
    ],
)


# The CLI binary target, one for each architecture.
pybind_library(
    name = "ecp5",
    srcs = [
        "ecp5/arch.cc",
        "ecp5/archdefs.h",
        "ecp5/arch.h",
        "ecp5/arch_place.cc",
        "ecp5/arch_pybindings.cc",
        "ecp5/arch_pybindings.h",
        "ecp5/baseconfigs.cc",
        "ecp5/bitstream.cc",
        "ecp5/bitstream.h",
        "ecp5/cells.cc",
        "ecp5/cells.h",
        "ecp5/config.cc",
        "ecp5/config.h",
        "ecp5/constids.inc",
        "ecp5/dcu_bitstream.h",
        "ecp5/gfx.cc",
        "ecp5/gfx.h",
        "ecp5/globals.cc",
        "ecp5/globals.h",
        "ecp5/iotypes.inc",
        "ecp5/lpf.cc",
        "ecp5/pack.cc",
        "ecp5/pio.cc",
        "ecp5/pio.h",
    ] + CORE_SRCS + CORE_HDRS,
    cxxopts = [
        "-std=c++17",
        "-fexceptions",
        "-Wno-unused-variable",
        "-Wno-implicit-conversion-floating-point-to-bool",
        "-Wno-implicit-fallthrough",
    ],
    defines = DEFINES + ECP5_DEFINES,
    includes = [
        "common/kernel",
        "common/route",
        "common/place",
        "common",
        "json",
        "frontend",
        "3rdparty/oourafft",
        "rust",
    ],
    linkopts = ["-lpthread"],
    visibility = ["//visibility:public"],
    deps = [
        ":ecp5_chipdb",
        "@eigen",
        "@json11",
        "@boost.filesystem",
        "@boost.algorithm",
        "@boost.functional",
        "@boost.lexical_cast",
        "@boost.thread",
        "@boost.iostreams",
        "@boost.program_options",
        "@boost.system",
    ],
)

cc_binary(
    name = "nextpnr-ecp5",
    srcs = [
        "ecp5/main.cc",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":ecp5",
        "@rules_python//python/cc:current_py_cc_libs"
    ],
)
