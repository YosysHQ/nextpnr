/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  Lofty <dan.ravensloft@gmail.com>
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
 */

#include <algorithm>

#include "log.h"
#include "nextpnr.h"

#include "cyclonev.h"

NEXTPNR_NAMESPACE_BEGIN

using namespace mistral;

void IdString::initialize_arch(const BaseCtx *ctx)
{
#define X(t) initialize_add(ctx, #t, ID_##t);

#include "constids.inc"

#undef X
}

Arch::Arch(ArchArgs args)
{
    this->args = args;
    this->cyclonev = mistral::CycloneV::get_model(args.device, args.mistral_root);
    NPNR_ASSERT(this->cyclonev != nullptr);

    // Setup fast identifier maps
    for (int i = 0; i < 1024; i++) {
        IdString int_id = id(stringf("%d", i));
        int2id.push_back(int_id);
        id2int[int_id] = i;
    }

    for (int t = int(CycloneV::NONE); t <= int(CycloneV::DCMUX); t++) {
        IdString rnode_id = id(CycloneV::rnode_type_names[t]);
        rn_t2id.push_back(rnode_id);
        id2rn_t[rnode_id] = CycloneV::rnode_type_t(t);
    }

    for (int x = 0; x < cyclonev->get_tile_sx(); x++) {
        for (int y = 0; y < cyclonev->get_tile_sy(); y++) {
            CycloneV::pos_t pos = cyclonev->xy2pos(x, y);

            for (CycloneV::block_type_t bel : cyclonev->pos_get_bels(pos)) {
                switch (bel) {
                case CycloneV::block_type_t::LAB:
                    /*
                     *  nextpnr and mistral disagree on what a BEL is: mistral thinks an entire LAB
                     *  is one BEL, but nextpnr wants something with more precision.
                     *
                     *  One LAB contains 10 ALMs.
                     *  One ALM contains 2 LUT outputs and 4 flop outputs.
                     */
                    for (int z = 0; z < 60; z++) {
                        bels[BelId(pos, (bel << 8 | z))];
                    }
                    break;
                case CycloneV::block_type_t::GPIO:
                    // GPIO tiles contain 4 pins.
                    for (int z = 0; z < 4; z++) {
                        bels[BelId(pos, (bel << 8 | z))];
                    }
                    break;
                default:
                    continue;
                }
            }
        }
    }

    BaseArch::init_cell_types();
    BaseArch::init_bel_buckets();
}

int Arch::getTileBelDimZ(int x, int y) const
{
    // FIXME: currently encoding type in z (this will be fixed soon when site contents are implemented)
    return 16384;
}

BelId Arch::getBelByName(IdStringList name) const
{
    BelId bel;
    NPNR_ASSERT(name.size() == 4);
    auto bel_type = cyclonev->block_type_lookup(name[0].str(this));
    int x = id2int.at(name[1]);
    int y = id2int.at(name[2]);
    int z = id2int.at(name[3]);

    bel.pos = CycloneV::xy2pos(x, y);
    bel.z = (bel_type << 8) | z;

    return bel;
}

IdStringList Arch::getBelName(BelId bel) const
{
    int x = CycloneV::pos2x(bel.pos);
    int y = CycloneV::pos2y(bel.pos);
    int z = bel.z & 0xFF;
    int bel_type = bel.z >> 8;

    std::array<IdString, 4> ids{
            id(cyclonev->block_type_names[bel_type]),
            int2id.at(x),
            int2id.at(y),
            int2id.at(z),
    };

    return IdStringList(ids);
}

WireId Arch::getWireByName(IdStringList name) const
{
    // non-mistral wires
    auto found_npnr = npnr_wirebyname.find(name);
    if (found_npnr != npnr_wirebyname.end())
        return found_npnr->second;
    // mistral wires
    NPNR_ASSERT(name.size() == 4);
    CycloneV::rnode_type_t ty = id2rn_t.at(name[0]);
    int x = id2int.at(name[1]);
    int y = id2int.at(name[2]);
    int z = id2int.at(name[3]);
    return WireId(CycloneV::rnode(ty, x, y, z));
}

IdStringList Arch::getWireName(WireId wire) const
{
    if (wire.is_nextpnr_created()) {
        // non-mistral wires
        std::array<IdString, 4> ids{
                id_WIRE,
                int2id.at(CycloneV::rn2x(wire.node)),
                int2id.at(CycloneV::rn2y(wire.node)),
                wires.at(wire).name_override,
        };
        return IdStringList(ids);
    } else {
        std::array<IdString, 4> ids{
                rn_t2id.at(CycloneV::rn2t(wire.node)),
                int2id.at(CycloneV::rn2x(wire.node)),
                int2id.at(CycloneV::rn2y(wire.node)),
                int2id.at(CycloneV::rn2z(wire.node)),
        };
        return IdStringList(ids);
    }
}

PipId Arch::getPipByName(IdStringList name) const
{
    WireId src = getWireByName(name.slice(0, 4));
    WireId dst = getWireByName(name.slice(4, 8));
    NPNR_ASSERT(src != WireId());
    NPNR_ASSERT(dst != WireId());
    return PipId(src.node, dst.node);
}

IdStringList Arch::getPipName(PipId pip) const
{
    return IdStringList::concat(getWireName(getPipSrcWire(pip)), getWireName(getPipDstWire(pip)));
}

std::vector<BelId> Arch::getBelsByTile(int x, int y) const
{
    // This should probably be redesigned, but it's a hack.
    std::vector<BelId> bels{};

    CycloneV::pos_t pos = cyclonev->xy2pos(x, y);

    for (CycloneV::block_type_t cvbel : cyclonev->pos_get_bels(pos)) {
        switch (cvbel) {
        case CycloneV::block_type_t::LAB:
            /*
             *  nextpnr and mistral disagree on what a BEL is: mistral thinks an entire LAB
             *  is one BEL, but nextpnr wants something with more precision.
             *
             *  One LAB contains 10 ALMs.
             *  One ALM contains 2 LUT outputs and 4 flop outputs.
             */
            for (int z = 0; z < 60; z++) {
                bels.push_back(BelId(pos, (cvbel << 8 | z)));
            }
            break;
        case CycloneV::block_type_t::GPIO:
            // GPIO tiles contain 4 pins.
            for (int z = 0; z < 4; z++) {
                bels.push_back(BelId(pos, (cvbel << 8 | z)));
            }
            break;
        default:
            continue;
        }
    }

    return bels;
}

IdString Arch::getBelType(BelId bel) const
{
    for (CycloneV::block_type_t cvbel : cyclonev->pos_get_bels(bel.pos)) {
        switch (cvbel) {
        case CycloneV::block_type_t::LAB:
            /*
             *  nextpnr and mistral disagree on what a BEL is: mistral thinks an entire LAB
             *  is one BEL, but nextpnr wants something with more precision.
             *
             *  One LAB contains 10 ALMs.
             *  One ALM contains 2 LUT outputs and 4 flop outputs.
             */
            return IdString(this, "LAB");
        case CycloneV::block_type_t::GPIO:
            // GPIO tiles contain 4 pins.
            return IdString(this, "GPIO");
        default:
            continue;
        }
    }

    return IdString();
}

bool Arch::pack() { return true; }
bool Arch::place() { return true; }
bool Arch::route() { return true; }

#ifdef WITH_HEAP
const std::string Arch::defaultPlacer = "heap";
#else
const std::string Arch::defaultPlacer = "sa";
#endif

const std::vector<std::string> Arch::availablePlacers = {"sa",
#ifdef WITH_HEAP
                                                         "heap"
#endif
};

const std::string Arch::defaultRouter = "router1";
const std::vector<std::string> Arch::availableRouters = {"router1", "router2"};

NEXTPNR_NAMESPACE_END