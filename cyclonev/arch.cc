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

#include "nextpnr.h"

#include "mistral/lib/cyclonev.h"

NEXTPNR_NAMESPACE_BEGIN

using namespace mistral;

Arch::Arch(ArchArgs args)
{
    this->args = args;
    this->cyclonev = mistral::CycloneV::get_model(args.device);
    NPNR_ASSERT(this->cyclonev != nullptr);

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
                        this->bel_list.push_back(BelId(pos, z));
                    }
                case CycloneV::block_type_t::GPIO:
                    // GPIO tiles contain 4 pins.
                    for (int z = 0; z < 4; z++) {
                        this->bel_list.push_back(BelId(pos, z));
                    }
                default:
                    continue;
                }
            }
        }
    }
}

int Arch::getTileBelDimZ(int x, int y) const
{
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
            return 60;
        case CycloneV::block_type_t::GPIO:
            // GPIO tiles contain 4 pins.
            return 4;
        default:
            continue;
        }
    }

    // As a temporary hack, only LABs and IO are allowed to be placed, so every other tile type has zero BELs.
    return 0;
}

BelId Arch::getBelByName(IdString name) const
{
    char bel_type_str[80] = {0};
    int x = 0, y = 0, z = 0;
    BelId bel;

    sscanf(name.c_str(this), "%25s.%d.%d.%d", bel_type_str, &x, &y, &z);

    auto bel_type = cyclonev->block_type_lookup(std::string{bel_type_str});

    bel.pos = CycloneV::xy2pos(x, y);
    bel.z = (bel_type << 8) | z;

    return bel;
}

IdString Arch::getBelName(BelId bel) const
{
    char bel_str[80] = {0};

    int x = CycloneV::pos2x(bel.pos);
    int y = CycloneV::pos2y(bel.pos);
    int z = bel.z & 0xFF;
    int bel_type = bel.z >> 8;

    snprintf(bel_str, 80, "%s.%03d.%03d.%03d", cyclonev->block_type_names[bel_type], x, y, z);

    return id(bel_str);
}

void Arch::bindBel(BelId bel, CellInfo *cell, PlaceStrength strength)
{
    bels.at(bel).bound_cell = cell;
    cell->bel = bel;
    cell->belStrength = strength;
    refreshUiBel(bel);
}

void Arch::unbindBel(BelId bel)
{
    bels.at(bel).bound_cell->bel = BelId();
    bels.at(bel).bound_cell->belStrength = STRENGTH_NONE;
    bels.at(bel).bound_cell = nullptr;
    refreshUiBel(bel);
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
                bels.push_back(BelId(pos, z));
            }
        case CycloneV::block_type_t::GPIO:
            // GPIO tiles contain 4 pins.
            for (int z = 0; z < 4; z++) {
                bels.push_back(BelId(pos, z));
            }
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

NEXTPNR_NAMESPACE_END