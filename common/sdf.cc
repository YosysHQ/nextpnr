/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2019  David Shah <dave@ds0.me>
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
        out << "(" << delay.min << ":" << delay.typ << ":" << delay.max << ")";
    }

    void write_port(std::ostream &out, const CellPort &port) { out << format_name(port.cell + "/" + port.port); }

    void write_portedge(std::ostream &out, const PortAndEdge &pe)
    {
        out << "(" << (pe.edge == RISING_EDGE ? "posedge" : "negedge") << " " << pe.port << ")";
    }

    void write(std::ostream &out)
    {
        out << "(DELAYFILE" << std::endl;
        // Headers and  metadata
        out << "  (SDFVERSION " << format_name(sdfversion) << ")" << std::endl;
        out << "  (DESIGN " << format_name(design) << ")" << std::endl;
        out << "  (VENDOR " << format_name(vendor) << ")" << std::endl;
        out << "  (PROGRAM " << format_name(program) << ")" << std::endl;
        out << "  (DIVIDER /)" << std::endl;
        out << "  (TIMESCALE 1ps)" << std::endl;
        // Write cells
        for (auto &cell : cells) {
            out << "  (CELL" << std::endl;
            out << "    (CELLTYPE " << format_name(cell.celltype) << ")" << std::endl;
            out << "    (INSTANCE " << format_name(cell.instance) << ")" << std::endl;
            // IOPATHs (combinational delay and clock-to-q)
            if (!cell.iopaths.empty()) {
                out << "    (DELAY" << std::endl;
                out << "      (ABSOLUTE" << std::endl;
                for (auto &path : cell.iopaths) {
                    out << "        (IOPATH " << path.from << " " << path.to << " ";
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
        // Write interconnect delays, with the main design begin a "cell"
        out << "  (CELL" << std::endl;
        out << "    (CELLTYPE " << format_name(design) << ")" << std::endl;
        out << "    (DELAY" << std::endl;
        out << "      (ABSOLUTE" << std::endl;
        for (auto &ic : conn) {
            out << "        (INTERCONNECT ";
            write_port(out, ic.from);
            out << " ";
            write_port(out, ic.to);
            out << " ";
            write_delay(out, ic.delay);
            out << std::endl;
        }
        out << "      )" << std::endl;
        out << "    )" << std::endl;
        out << "  )" << std::endl;
        out << ")" << std::endl;
    }
};

} // namespace SDF

NEXTPNR_NAMESPACE_END