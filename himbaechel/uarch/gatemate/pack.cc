/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2024  The Project Peppercorn Authors.
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

#include "pack.h"

#define HIMBAECHEL_CONSTIDS "uarch/gatemate/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

void GateMatePacker::disconnect_if_gnd(CellInfo *cell, IdString input)
{
    NetInfo *net = cell->getPort(input);
    if (!net)
        return;
    if (net->name.in(ctx->id("$PACKER_GND"))) {
        cell->disconnectPort(input);
    }
}
void GateMatePacker::pack_io()
{
    // Trim nextpnr IOBs - assume IO buffer insertion has been done in synthesis
    for (auto &port : ctx->ports) {
        if (!ctx->cells.count(port.first))
            log_error("Port '%s' doesn't seem to have a corresponding top level IO\n", ctx->nameOf(port.first));
        CellInfo *ci = ctx->cells.at(port.first).get();

        PortRef top_port;
        top_port.cell = nullptr;
        bool is_npnr_iob = false;

        if (ci->type == ctx->id("$nextpnr_ibuf") || ci->type == ctx->id("$nextpnr_iobuf")) {
            // Might have an input buffer connected to it
            is_npnr_iob = true;
            NetInfo *o = ci->getPort(id_O);
            if (o == nullptr)
                ;
            else if (o->users.entries() > 1)
                log_error("Top level pin '%s' has multiple input buffers\n", ctx->nameOf(port.first));
            else if (o->users.entries() == 1)
                top_port = *o->users.begin();
        }
        if (ci->type == ctx->id("$nextpnr_obuf") || ci->type == ctx->id("$nextpnr_iobuf")) {
            // Might have an output buffer connected to it
            is_npnr_iob = true;
            NetInfo *i = ci->getPort(id_I);
            if (i != nullptr && i->driver.cell != nullptr) {
                if (top_port.cell != nullptr)
                    log_error("Top level pin '%s' has multiple input/output buffers\n", ctx->nameOf(port.first));
                top_port = i->driver;
            }
            // Edge case of a bidirectional buffer driving an output pin
            if (i->users.entries() > 2) {
                log_error("Top level pin '%s' has illegal buffer configuration\n", ctx->nameOf(port.first));
            } else if (i->users.entries() == 2) {
                if (top_port.cell != nullptr)
                    log_error("Top level pin '%s' has illegal buffer configuration\n", ctx->nameOf(port.first));
                for (auto &usr : i->users) {
                    if (usr.cell->type == ctx->id("$nextpnr_obuf") || usr.cell->type == ctx->id("$nextpnr_iobuf"))
                        continue;
                    top_port = usr;
                    break;
                }
            }
        }
        if (!is_npnr_iob)
            log_error("Port '%s' doesn't seem to have a corresponding top level IO (internal cell type mismatch)\n",
                      ctx->nameOf(port.first));

        if (top_port.cell == nullptr) {
            log_info("Trimming port '%s' as it is unused.\n", ctx->nameOf(port.first));
        } else {
            // Copy attributes to real IO buffer
            for (auto &attrs : ci->attrs)
                top_port.cell->attrs[attrs.first] = attrs.second;
            for (auto &params : ci->params) {
                if (top_port.cell->params.count(params.first)) {
                    if (top_port.cell->params[params.first] != params.second) {
                        std::string val = params.second.is_string ? params.second.as_string()
                                                                  : std::to_string(params.second.as_int64());
                        log_warning("Overriding parameter '%s' with value '%s' for cell '%s'.\n",
                                    params.first.c_str(ctx), val.c_str(), ctx->nameOf(top_port.cell));
                    }
                }
                top_port.cell->params[params.first] = params.second;
            }

            // Make sure that top level net is set correctly
            port.second.net = top_port.cell->ports.at(top_port.port).net;
        }
        // Now remove the nextpnr-inserted buffer
        ci->disconnectPort(id_I);
        ci->disconnectPort(id_O);
        ctx->cells.erase(port.first);
    }

    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_IBUF, id_CC_OBUF, id_CC_TOBUF, id_CC_IOBUF))
            continue;
        std::string loc = str_or_default(ci.params, id_PIN_NAME, "UNPLACED");
        if (loc == "UNPLACED")
            log_warning("IO signal name '%s' is not defined in CCF file and will be auto-placed.\n", ctx->nameOf(&ci));

        disconnect_if_gnd(&ci, id_T);
        if (ci.type == id_CC_TOBUF && !ci.getPort(id_T))
            ci.type = id_CC_OBUF;

        std::vector<IdString> keys;
        for (auto &p : ci.params) {
            if (p.first.in(id_PIN_NAME, id_V_IO)) {
                keys.push_back(p.first);
                continue;
            }
            if (ci.type.in(id_CC_IBUF, id_CC_IOBUF) &&
                p.first.in(id_PULLUP, id_PULLDOWN, id_KEEPER, id_SCHMITT_TRIGGER, id_DELAY_IBF, id_FF_IBF))
                continue;
            if (ci.type.in(id_CC_TOBUF) && p.first.in(id_PULLUP, id_PULLDOWN, id_KEEPER))
                continue;
            if (ci.type.in(id_CC_OBUF, id_CC_TOBUF, id_CC_IOBUF) &&
                p.first.in(id_DRIVE, id_SLEW, id_DELAY_OBF, id_FF_OBF))
                continue;
            log_warning("Removing unsupported parameter '%s' for type '%s'.\n", p.first.c_str(ctx), ci.type.c_str(ctx));
            keys.push_back(p.first);
        }
        if (ci.params.count(id_SLEW)) {
            std::string val = str_or_default(ci.params, id_SLEW, "UNDEFINED");
            if (val == "UNDEFINED")
                keys.push_back(id_SLEW);
            else if (val == "FAST")
                ci.params[id_SLEW] = Property(Property::State::S1);
            else if (val == "SLOW")
                ci.params[id_SLEW] = Property(Property::State::S0);
            else
                log_error("Unknown value '%s' for SLEW parameter of '%s' cell.\n", val.c_str(), ci.name.c_str(ctx));
        }
        for (auto key : keys)
            ci.params.erase(key);

        if ((ci.params.count(id_KEEPER) + ci.params.count(id_PULLUP) + ci.params.count(id_PULLDOWN)) > 1)
            log_error("PULLUP, PULLDOWN and KEEPER are mutually exclusive parameters.\n");

        // DELAY_IBF and DELAY_OBF must be set depending of type
        // Also we need to enable input/output
        if (ci.type.in(id_CC_IBUF, id_CC_IOBUF)) {
            ci.params[id_DELAY_IBF] = Property(1 << int_or_default(ci.params, id_DELAY_IBF, 0), 16);
            ci.params[id_INPUT_ENABLE] = Property(Property::State::S1);
        }
        if (ci.type.in(id_CC_OBUF, id_CC_TOBUF, id_CC_IOBUF)) {
            ci.params[id_DELAY_OBF] = Property(1 << int_or_default(ci.params, id_DELAY_OBF, 0), 16);
            ci.params[id_OE_ENABLE] = Property(Property::State::S1);
        }
        if (ci.params.count(id_DRIVE)) {
            int val = int_or_default(ci.params, id_DRIVE, 0);
            if (val != 3 && val != 6 && val != 9 && val != 12)
                log_error("Unsupported value '%d' for DRIVE parameter of '%s' cell.\n", val, ci.name.c_str(ctx));
            ci.params[id_DRIVE] = Property((val - 3) / 3, 2);
        }
        for (auto &p : ci.params) {
            if (p.first.in(id_PULLUP, id_PULLDOWN, id_KEEPER, id_SCHMITT_TRIGGER, id_FF_OBF, id_FF_IBF)) {
                int val = int_or_default(ci.params, p.first, 0);
                if (val != 0 && val != 1)
                    log_error("Unsupported value '%d' for %s parameter of '%s' cell.\n", val, p.first.c_str(ctx),
                              ci.name.c_str(ctx));
                ci.params[p.first] = Property(val, 1);
            }
        }

        // Disconnect PADs
        ci.disconnectPort(id_IO);
        ci.disconnectPort(id_I);
        ci.disconnectPort(id_O);

        // Remap ports to GPIO bel
        ci.renamePort(id_A, id_DO);
        ci.renamePort(id_Y, id_DI);
        ci.renamePort(id_T, id_OE);

        NetInfo *do_net = ci.getPort(id_DO);
        if (do_net) {
            if (do_net->name.in(ctx->id("$PACKER_GND"), ctx->id("$PACKER_VCC"))) {
                ci.params[id_OUT23_14_SEL] =
                        Property(do_net->name == ctx->id("$PACKER_VCC") ? Property::State::S1 : Property::State::S0);
                ci.disconnectPort(id_DO);
            } else {
                ci.params[id_OUT_SIGNAL] = Property(Property::State::S1);
            }
        }
        if (!loc.empty()) {
            BelId bel = ctx->get_package_pin_bel(ctx->id(loc));
            if (bel == BelId())
                log_error("Unable to constrain IO '%s', device does not have a pin named '%s'\n", ci.name.c_str(ctx),
                          loc.c_str());
            log_info("    Constraining '%s' to pad '%s'\n", ci.name.c_str(ctx), loc.c_str());

            ctx->bindBel(bel, &ci, PlaceStrength::STRENGTH_FIXED);
        }
    }
}

void GateMatePacker::pack_constants()
{
    log_info("Packing constants..\n");
    // Replace constants with LUTs
    const dict<IdString, Property> vcc_params = {
            {id_INIT_L00, Property(0xf, 4)}, {id_INIT_L01, Property(0xf, 4)}, {id_INIT_L02, Property(0xf, 4)}};
    const dict<IdString, Property> gnd_params = {
            {id_INIT_L00, Property(0x0, 4)}, {id_INIT_L01, Property(0x0, 4)}, {id_INIT_L02, Property(0x0, 4)}};

    h.replace_constants(CellTypePort(id_CPE, id_OUT1), CellTypePort(id_CPE, id_OUT1), vcc_params, gnd_params);
}

void GateMatePacker::remove_constants()
{
    log_info("Removing constants..\n");
    auto fnd_cell = ctx->cells.find(ctx->id("$PACKER_VCC_DRV"));
    if (fnd_cell != ctx->cells.end()) {
        auto fnd_net = ctx->nets.find(ctx->id("$PACKER_VCC"));
        if (fnd_net != ctx->nets.end() && fnd_net->second->users.entries() == 0) {
            BelId bel = (*fnd_cell).second.get()->bel;
            if (bel != BelId())
                ctx->unbindBel(bel);
            ctx->cells.erase(fnd_cell);
            ctx->nets.erase(fnd_net);
            log_info("    Removed unused VCC cell\n");
        }
    }
    fnd_cell = ctx->cells.find(ctx->id("$PACKER_GND_DRV"));
    if (fnd_cell != ctx->cells.end()) {
        auto fnd_net = ctx->nets.find(ctx->id("$PACKER_GND"));
        if (fnd_net != ctx->nets.end() && fnd_net->second->users.entries() == 0) {
            BelId bel = (*fnd_cell).second.get()->bel;
            if (bel != BelId())
                ctx->unbindBel(bel);
            ctx->cells.erase(fnd_cell);
            ctx->nets.erase(fnd_net);
            log_info("    Removed unused GND cell\n");
        }
    }
}

void GateMateImpl::pack()
{
    const ArchArgs &args = ctx->args;
    if (args.options.count("ccf")) {
        parse_ccf(args.options.at("ccf"));
    }

    GateMatePacker packer(ctx, this);
    packer.pack_constants();
    packer.pack_io();
    packer.remove_constants();
}

NEXTPNR_NAMESPACE_END
