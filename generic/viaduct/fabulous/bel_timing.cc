/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021-2026  The FABulous maintainers <fpga.research.group@gmail.com>
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

#include "bel_timing.h"

#include "log.h"
#include "util.h"

#define VIADUCT_CONSTIDS "viaduct/fabulous/constids.inc"
#include "viaduct_constids.h"

NEXTPNR_NAMESPACE_BEGIN

float parse_float(parser_view v)
{
    std::string s(v.m_ptr, v.m_length);
    try {
        return std::stof(s);
    } catch (const std::exception &) {
        log_error("invalid bel.v3 timing value '%s'\n", s.c_str());
    }
}

double parse_double(parser_view v)
{
    std::string s(v.m_ptr, v.m_length);
    try {
        return std::stod(s);
    } catch (const std::exception &) {
        log_error("invalid placement_estimate.txt value '%s'\n", s.c_str());
    }
}

std::vector<TimingPredicate> parse_timing_condition(Context *ctx, parser_view field)
{
    std::vector<TimingPredicate> preds;
    if (field.empty())
        return preds;
    std::string cond(field.m_ptr, field.m_length);
    size_t start = 0;
    while (start <= cond.size()) {
        size_t amp = cond.find('&', start);
        std::string tok = cond.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
        start = (amp == std::string::npos) ? cond.size() + 1 : amp + 1;
        if (tok.empty())
            continue;
        TimingPredicate p;
        char last = tok.back();
        if (last == '?' || last == '!') {
            p.kind = (last == '?') ? TimingPredicate::PORT_ANY_CONNECTED : TimingPredicate::PORT_NONE_CONNECTED;
            std::string ports = tok.substr(0, tok.size() - 1);
            size_t ps = 0;
            while (ps <= ports.size()) {
                size_t slash = ports.find('/', ps);
                std::string pn = ports.substr(ps, slash == std::string::npos ? std::string::npos : slash - ps);
                ps = (slash == std::string::npos) ? ports.size() + 1 : slash + 1;
                if (!pn.empty())
                    p.ports.push_back(ctx->id(pn));
            }
        } else {
            size_t eq = tok.find('=');
            if (eq == std::string::npos)
                log_error("invalid bel.v3 timing condition '%s'\n", tok.c_str());
            p.kind = TimingPredicate::PARAM;
            p.param = ctx->id(tok.substr(0, eq));
            p.param_value = (tok.substr(eq + 1) != "0");
        }
        preds.push_back(std::move(p));
    }
    return preds;
}

BelTimingArc parse_one_arc(Context *ctx, CsvParser &csv, IdString cmd)
{
    BelTimingArc arc;
    if (cmd == id_Delay) {
        arc.kind = BelTimingArc::DELAY;
        arc.from = csv.next_field().to_id(ctx);
        arc.to = csv.next_field().to_id(ctx);
        arc.v0 = parse_float(csv.next_field());
    } else if (cmd == id_SetupHold) {
        arc.kind = BelTimingArc::SETUPHOLD;
        arc.from = csv.next_field().to_id(ctx);
        arc.to = csv.next_field().to_id(ctx);
        arc.v0 = parse_float(csv.next_field());
        arc.v1 = parse_float(csv.next_field());
    } else if (cmd == id_ClkToOut) {
        arc.kind = BelTimingArc::CLK2OUT;
        arc.from = csv.next_field().to_id(ctx);
        arc.to = csv.next_field().to_id(ctx);
        arc.v0 = parse_float(csv.next_field());
    } else {
        NPNR_ASSERT(cmd == id_Clock);
        arc.kind = BelTimingArc::CLOCK;
        arc.from = csv.next_field().to_id(ctx);
    }
    arc.cond = parse_timing_condition(ctx, csv.next_field());
    return arc;
}

bool timing_cond_holds(const CellInfo *ci, const std::vector<TimingPredicate> &cond)
{
    for (const auto &p : cond) {
        if (p.kind == TimingPredicate::PARAM) {
            if (bool_or_default(ci->params, p.param) != p.param_value)
                return false;
        } else if (p.kind == TimingPredicate::PORT_ANY_CONNECTED) {
            bool any = false;
            for (IdString port : p.ports)
                if (ci->getPort(port)) {
                    any = true;
                    break;
                }
            if (!any)
                return false;
        } else { // PORT_NONE_CONNECTED
            for (IdString port : p.ports)
                if (ci->getPort(port))
                    return false;
        }
    }
    return true;
}

void apply_arc(Context *ctx, CellInfo *ci, const BelTimingArc &arc)
{
    if (!timing_cond_holds(ci, arc.cond))
        return;
    switch (arc.kind) {
    case BelTimingArc::DELAY:
        ctx->addCellTimingDelay(ci->name, arc.from, arc.to, arc.v0);
        break;
    case BelTimingArc::SETUPHOLD:
        ctx->addCellTimingSetupHold(ci->name, arc.from, arc.to, arc.v0, arc.v1);
        break;
    case BelTimingArc::CLK2OUT:
        ctx->addCellTimingClockToOut(ci->name, arc.from, arc.to, arc.v0);
        break;
    case BelTimingArc::CLOCK:
        ctx->addCellTimingClock(ci->name, arc.from);
        break;
    }
}

NEXTPNR_NAMESPACE_END
