/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2019  gatecat <gatecat@ds0.me>
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

#include "nextpnr.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

namespace SDF {

struct MinMaxTyp
{
    double min, typ, max;
};

struct RiseFallDelay
{
    MinMaxTyp rise, fall;
};

struct PortAndEdge
{
    std::string port;
    ClockEdge edge;
};

struct IOPath
{
    std::string from, to;
    RiseFallDelay delay;
};

struct TimingCheck
{
    enum CheckType
    {
        SETUPHOLD,
        PERIOD,
        WIDTH
    } type;
    PortAndEdge from, to;
    RiseFallDelay delay;
};

struct Cell
{
    std::string celltype, instance;
    std::vector<IOPath> iopaths;
    std::vector<TimingCheck> checks;
};

struct CellPort
{
    std::string cell, port;
};

struct Interconnect
{
    CellPort from, to;
    RiseFallDelay delay;
};

struct SDFWriter
{
    bool cvc_mode = false;
    std::vector<Cell> cells;
    std::vector<Interconnect> conn;
    std::string sdfversion, design, vendor, program;

    std::string format_name(const std::string &name)
    {
        std::string fmt = "\"";
        for (char c : name) {
            if (c == '\\' || c == '\"')
                fmt += "\"";
            fmt += c;
        }
        fmt += "\"";
        return fmt;
    }

    std::string escape_name(const std::string &name)
    {
        std::string esc;
        for (char c : name) {
            if (c == '$' || c == '\\' || c == '[' || c == ']' || c == ':' || (cvc_mode && c == '.'))
                esc += '\\';
            esc += c;
        }
        return esc;
    }

    std::string timing_check_name(TimingCheck::CheckType type)
    {
        switch (type) {
        case TimingCheck::SETUPHOLD:
            return "SETUPHOLD";
        case TimingCheck::PERIOD:
            return "PERIOD";
        case TimingCheck::WIDTH:
            return "WIDTH";
        default:
            NPNR_ASSERT_FALSE("unknown timing check type");
        }
    }

    void write_delay(std::ostream &out, const RiseFallDelay &delay)
    {
        write_delay(out, delay.rise);
        out << " ";
        write_delay(out, delay.fall);
    }

    void write_delay(std::ostream &out, const MinMaxTyp &delay)
    {
        if (cvc_mode)
            out << "(" << int(delay.min) << ":" << int(delay.typ) << ":" << int(delay.max) << ")";
        else
            out << "(" << delay.min << ":" << delay.typ << ":" << delay.max << ")";
    }

    void write_port(std::ostream &out, const CellPort &port)
    {
        if (cvc_mode)
            out << escape_name(port.cell) + "." + escape_name(port.port);
        else
            out << escape_name(port.cell + "/" + port.port);
    }

    void write_portedge(std::ostream &out, const PortAndEdge &pe)
    {
        out << "(" << (pe.edge == RISING_EDGE ? "posedge" : "negedge") << " " << escape_name(pe.port) << ")";
    }

    void write(std::ostream &out)
    {
        out << "(DELAYFILE" << std::endl;
        // Headers and  metadata
        out << "  (SDFVERSION " << format_name(sdfversion) << ")" << std::endl;
        out << "  (DESIGN " << format_name(design) << ")" << std::endl;
        out << "  (VENDOR " << format_name(vendor) << ")" << std::endl;
        out << "  (PROGRAM " << format_name(program) << ")" << std::endl;
        out << "  (DIVIDER " << (cvc_mode ? "." : "/") << ")" << std::endl;
        out << "  (TIMESCALE 1ps)" << std::endl;
        // Write interconnect delays, with the main design begin a "cell"
        out << "  (CELL" << std::endl;
        out << "    (CELLTYPE " << format_name(design) << ")" << std::endl;
        out << "    (INSTANCE )" << std::endl;
        out << "    (DELAY" << std::endl;
        out << "      (ABSOLUTE" << std::endl;
        for (auto &ic : conn) {
            out << "        (INTERCONNECT ";
            write_port(out, ic.from);
            out << " ";
            write_port(out, ic.to);
            out << " ";
            write_delay(out, ic.delay);
            out << ")" << std::endl;
        }
        out << "      )" << std::endl;
        out << "    )" << std::endl;
        out << "  )" << std::endl;
        // Write cells
        for (auto &cell : cells) {
            out << "  (CELL" << std::endl;
            out << "    (CELLTYPE " << format_name(cell.celltype) << ")" << std::endl;
            out << "    (INSTANCE " << escape_name(cell.instance) << ")" << std::endl;
            // IOPATHs (combinational delay and clock-to-q)
            if (!cell.iopaths.empty()) {
                out << "    (DELAY" << std::endl;
                out << "      (ABSOLUTE" << std::endl;
                for (auto &path : cell.iopaths) {
                    out << "        (IOPATH " << escape_name(path.from) << " " << escape_name(path.to) << " ";
                    write_delay(out, path.delay);
                    out << ")" << std::endl;
                }
                out << "      )" << std::endl;
                out << "    )" << std::endl;
            }
            // Timing Checks (setup/hold, period, width)
            if (!cell.checks.empty()) {
                out << "    (TIMINGCHECK" << std::endl;
                for (auto &check : cell.checks) {
                    out << "      (" << timing_check_name(check.type) << " ";
                    write_portedge(out, check.from);
                    out << " ";
                    if (check.type == TimingCheck::SETUPHOLD) {
                        write_portedge(out, check.to);
                        out << " ";
                    }
                    if (check.type == TimingCheck::SETUPHOLD)
                        write_delay(out, check.delay);
                    else
                        write_delay(out, check.delay.rise);
                    out << ")" << std::endl;
                }
                out << "    )" << std::endl;
            }
            out << "    )" << std::endl;
        }
        out << ")" << std::endl;
    }
};

} // namespace SDF

void Context::writeSDF(std::ostream &out, bool cvc_mode) const
{
    using namespace SDF;
    SDFWriter wr;
    wr.cvc_mode = cvc_mode;
    wr.design = str_or_default(attrs, id("module"), "top");
    wr.sdfversion = "3.0";
    wr.vendor = "nextpnr";
    wr.program = "nextpnr";

    const double delay_scale = 1000;
    // Convert from DelayQuad to SDF-friendly RiseFallDelay
    auto convert_delay = [&](const DelayQuad &dly) {
        RiseFallDelay rf;
        rf.rise.min = getDelayNS(dly.minRiseDelay()) * delay_scale;
        rf.rise.typ = getDelayNS((dly.minRiseDelay() + dly.maxRiseDelay()) / 2) * delay_scale; // fixme: typ delays?
        rf.rise.max = getDelayNS(dly.maxRiseDelay()) * delay_scale;
        rf.fall.min = getDelayNS(dly.minFallDelay()) * delay_scale;
        rf.fall.typ = getDelayNS((dly.minFallDelay() + dly.maxFallDelay()) / 2) * delay_scale; // fixme: typ delays?
        rf.fall.max = getDelayNS(dly.maxFallDelay()) * delay_scale;
        return rf;
    };

    auto convert_setuphold = [&](const DelayPair &setup, const DelayPair &hold) {
        RiseFallDelay rf;
        rf.rise.min = getDelayNS(setup.minDelay()) * delay_scale;
        rf.rise.typ = getDelayNS((setup.minDelay() + setup.maxDelay()) / 2) * delay_scale; // fixme: typ delays?
        rf.rise.max = getDelayNS(setup.maxDelay()) * delay_scale;
        rf.fall.min = getDelayNS(hold.minDelay()) * delay_scale;
        rf.fall.typ = getDelayNS((hold.minDelay() + hold.maxDelay()) / 2) * delay_scale; // fixme: typ delays?
        rf.fall.max = getDelayNS(hold.maxDelay()) * delay_scale;
        return rf;
    };

    for (const auto &cell : cells) {
        Cell sc;
        const CellInfo *ci = cell.second.get();
        sc.instance = ci->name.str(this);
        sc.celltype = ci->type.str(this);
        for (auto port : ci->ports) {
            int clockCount = 0;
            TimingPortClass cls = getPortTimingClass(ci, port.first, clockCount);
            if (cls == TMG_IGNORE)
                continue;
            if (port.second.net == nullptr)
                continue; // Ignore disconnected ports
            if (port.second.type != PORT_IN) {
                // Add combinational paths to this output (or inout)
                for (auto other : ci->ports) {
                    if (other.second.net == nullptr)
                        continue;
                    if (other.second.type == PORT_OUT)
                        continue;
                    DelayQuad dly;
                    if (!getCellDelay(ci, other.first, port.first, dly))
                        continue;
                    IOPath iop;
                    iop.from = other.first.str(this);
                    iop.to = port.first.str(this);
                    iop.delay = convert_delay(dly);
                    sc.iopaths.push_back(iop);
                }
                // Add clock-to-output delays, also as IOPaths
                if (cls == TMG_REGISTER_OUTPUT)
                    for (int i = 0; i < clockCount; i++) {
                        auto clkInfo = getPortClockingInfo(ci, port.first, i);
                        IOPath cqp;
                        cqp.from = clkInfo.clock_port.str(this);
                        cqp.to = port.first.str(this);
                        cqp.delay = convert_delay(clkInfo.clockToQ);
                        sc.iopaths.push_back(cqp);
                    }
            }
            if (port.second.type != PORT_OUT && cls == TMG_REGISTER_INPUT) {
                // Add setup/hold checks
                for (int i = 0; i < clockCount; i++) {
                    auto clkInfo = getPortClockingInfo(ci, port.first, i);
                    TimingCheck chk;
                    chk.from.edge = RISING_EDGE; // Add setup/hold checks equally for rising and falling edges
                    chk.from.port = port.first.str(this);
                    chk.to.edge = clkInfo.edge;
                    chk.to.port = clkInfo.clock_port.str(this);
                    chk.type = TimingCheck::SETUPHOLD;
                    chk.delay = convert_setuphold(clkInfo.setup, clkInfo.hold);
                    sc.checks.push_back(chk);
                    chk.from.edge = FALLING_EDGE;
                    sc.checks.push_back(chk);
                }
            }
        }
        wr.cells.push_back(sc);
    }

    for (auto &net : nets) {
        NetInfo *ni = net.second.get();
        if (ni->driver.cell == nullptr)
            continue;
        for (auto &usr : ni->users) {
            Interconnect ic;
            ic.from.cell = ni->driver.cell->name.str(this);
            ic.from.port = ni->driver.port.str(this);
            ic.to.cell = usr.cell->name.str(this);
            ic.to.port = usr.port.str(this);
            // FIXME: min/max routing delay
            ic.delay = convert_delay(getNetinfoRouteDelayQuad(ni, usr));
            wr.conn.push_back(ic);
        }
    }
    wr.write(out);
}

NEXTPNR_NAMESPACE_END
