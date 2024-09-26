/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
 *  Copyright (C) 2018  Serge Bazanski <q3k@q3k.org>
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#ifndef CONTEXT_H
#define CONTEXT_H

#include <boost/lexical_cast.hpp>

#include "arch.h"
#include "deterministic_rng.h"

NEXTPNR_NAMESPACE_BEGIN

struct Context : Arch, DeterministicRNG
{
    bool verbose = false;
    bool debug = false;
    bool force = false;

    // Should we disable printing of the location of nets in the critical path?
    bool disable_critical_path_source_print = false;
    // True when detailed per-net timing is to be stored / reported
    bool detailed_timing_report = false;
    // Default to true, will update when timing analysis is run
    bool target_frequency_achieved = true;

    ArchArgs arch_args;

    Context(ArchArgs args) : Arch(args)
    {
        BaseCtx::as_ctx = this;
        arch_args = args;
    }

    ArchArgs getArchArgs() { return arch_args; }

    // --------------------------------------------------------------

    delay_t predictArcDelay(const NetInfo *net_info, const PortRef &sink) const;

    WireId getNetinfoSourceWire(const NetInfo *net_info) const;
    SSOArray<WireId, 2> getNetinfoSinkWires(const NetInfo *net_info, const PortRef &sink) const;
    size_t getNetinfoSinkWireCount(const NetInfo *net_info, const PortRef &sink) const;
    WireId getNetinfoSinkWire(const NetInfo *net_info, const PortRef &sink, size_t phys_idx) const;
    delay_t getNetinfoRouteDelay(const NetInfo *net_info, const PortRef &sink) const;
    DelayQuad getNetinfoRouteDelayQuad(const NetInfo *net_info, const PortRef &sink) const;

    // provided by router1.cc
    bool checkRoutedDesign() const;
    bool getActualRouteDelay(WireId src_wire, WireId dst_wire, delay_t *delay = nullptr,
                             dict<WireId, PipId> *route = nullptr, bool useEstimate = true);

    // --------------------------------------------------------------
    // Dispatch to the Arch API or pseudo-cell API accordingly
    bool getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayQuad &delay) const override
    {
        return cell->pseudo_cell ? cell->pseudo_cell->getDelay(fromPort, toPort, delay)
                                 : Arch::getCellDelay(cell, fromPort, toPort, delay);
    }
    TimingPortClass getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const override
    {
        return cell->pseudo_cell ? cell->pseudo_cell->getPortTimingClass(port, clockInfoCount)
                                 : Arch::getPortTimingClass(cell, port, clockInfoCount);
    }
    TimingClockingInfo getPortClockingInfo(const CellInfo *cell, IdString port, int index) const override
    {
        return cell->pseudo_cell ? cell->pseudo_cell->getPortClockingInfo(port, index)
                                 : Arch::getPortClockingInfo(cell, port, index);
    }

    // --------------------------------------------------------------
    // call after changing hierpath or adding/removing nets and cells
    void fixupHierarchy();

    // --------------------------------------------------------------

    // provided by sdf.cc
    void writeSDF(std::ostream &out, bool cvc_mode = false) const;

    // --------------------------------------------------------------

    // provided by svg.cc
    void writeSVG(const std::string &filename, const std::string &flags = "") const;

    // --------------------------------------------------------------

    // provided by report.cc
    void writeJsonReport(std::ostream &out) const;

    // provided by timing_log.cc
    void log_timing_results(TimingResult &result, bool print_histogram, bool print_fmax, bool print_path,
                            bool warn_on_failure);

    // provided by sdc.cc
    void read_sdc(std::istream &in);

    // --------------------------------------------------------------

    uint32_t checksum() const;

    void check() const;
    void archcheck() const;

    template <typename T> T setting(const char *name, T defaultValue)
    {
        IdString new_id = id(name);
        auto found = settings.find(new_id);
        if (found != settings.end())
            return boost::lexical_cast<T>(found->second.is_string ? found->second.as_string()
                                                                  : std::to_string(found->second.as_int64()));
        else
            settings[id(name)] = std::to_string(defaultValue);

        return defaultValue;
    }

    template <typename T> T setting(const char *name) const
    {
        IdString new_id = id(name);
        auto found = settings.find(new_id);
        if (found != settings.end())
            return boost::lexical_cast<T>(found->second.is_string ? found->second.as_string()
                                                                  : std::to_string(found->second.as_int64()));
        else
            throw std::runtime_error("settings does not exists");
    }
};

NEXTPNR_NAMESPACE_END

#endif /* CONTEXT_H */
