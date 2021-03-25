/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  Symbiflow Authors
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

#include "log.h"
#include "luts.h"

NEXTPNR_NAMESPACE_BEGIN

bool rotate_and_merge_lut_equation(std::vector<LogicLevel> *result, const LutBel &lut_bel,
                                   const DynamicBitarray<> &old_equation, const std::vector<int32_t> &pin_map,
                                   uint32_t used_pins)
{
    // pin_map maps pin indicies from the old pin to the new pin.
    // So a reversal of a LUT4 would have a pin map of:
    // pin_map[0] = 3;
    // pin_map[1] = 2;
    // pin_map[2] = 1;
    // pin_map[3] = 0;

    size_t bel_width = 1 << lut_bel.pins.size();
    for (size_t bel_address = 0; bel_address < bel_width; ++bel_address) {
        bool address_reachable = true;
        size_t cell_address = 0;
        for (size_t bel_pin_idx = 0; bel_pin_idx < lut_bel.pins.size(); ++bel_pin_idx) {
            // This address line is 0, so don't translate this bit to the cell
            // address.
            if ((bel_address & (1 << bel_pin_idx)) == 0) {
                // This pin is unused, so the line will be tied high, this
                // address is unreachable.
                //
                // FIXME: The assumption is that unused pins are tied VCC.
                // This is not generally true.
                //
                // Use Arch::prefered_constant_net_type to determine what
                // constant net should be used for unused pins.
                if ((used_pins & (1 << bel_pin_idx)) == 0) {
                    address_reachable = false;
                    break;
                }

                continue;
            }

            auto cell_pin_idx = pin_map[bel_pin_idx];

            // Is this BEL pin used for this cell?
            if (cell_pin_idx < 0) {
                // This BEL pin is not used for the LUT cell, skip
                continue;
            }

            cell_address |= (1 << cell_pin_idx);
        }

        if (!address_reachable) {
            continue;
        }

        bel_address += lut_bel.low_bit;
        if (old_equation.get(cell_address)) {
            if ((*result)[bel_address] == LL_Zero) {
                // Output equation has a conflict!
                return false;
            }

            (*result)[bel_address] = LL_One;
        } else {
            if ((*result)[bel_address] == LL_One) {
                // Output equation has a conflict!
                return false;
            }
            (*result)[bel_address] = LL_Zero;
        }
    }

    return true;
}

static constexpr bool kCheckOutputEquation = true;

struct LutPin
{
    struct LutPinUser
    {
        size_t cell_idx;
        size_t cell_pin_idx;
    };

    const NetInfo *net = nullptr;
    std::vector<LutPinUser> users;

    int32_t min_pin = -1;
    int32_t max_pin = -1;

    void add_user(const LutBel &lut_bel, size_t cell_idx, size_t cell_pin_idx)
    {
        if (min_pin < 0) {
            min_pin = lut_bel.min_pin;
            max_pin = lut_bel.max_pin;
        }

        min_pin = std::max(min_pin, lut_bel.min_pin);
        max_pin = std::min(max_pin, lut_bel.max_pin);

        users.emplace_back();
        users.back().cell_idx = cell_idx;
        users.back().cell_pin_idx = cell_pin_idx;
    }

    bool operator<(const LutPin &other) const { return max_pin < other.max_pin; }
};

//#define DEBUG_LUT_ROTATION

uint32_t LutMapper::check_wires(const std::vector<std::vector<int32_t>> &bel_to_cell_pin_remaps,
                                const std::vector<const LutBel *> &lut_bels, uint32_t used_pins) const
{
    std::vector<const LutBel *> unused_luts;
    for (auto &lut_bel_pair : element.lut_bels) {
        if (std::find(lut_bels.begin(), lut_bels.end(), &lut_bel_pair.second) == lut_bels.end()) {
            unused_luts.push_back(&lut_bel_pair.second);
        }
    }

    // FIXME: The assumption is that unused pins are tied VCC.
    // This is not generally true.
    //
    // Use Arch::prefered_constant_net_type to determine what
    // constant net should be used for unused pins.
    uint32_t vcc_mask = 0;

    DynamicBitarray<> wire_equation;
    wire_equation.resize(2);
    wire_equation.set(0, false);
    wire_equation.set(1, true);

    std::vector<int32_t> wire_bel_to_cell_pin_map;
    std::vector<LogicLevel> equation_result;
    for (int32_t pin_idx = 0; pin_idx < (int32_t)element.pins.size(); ++pin_idx) {
        if (used_pins & (1 << pin_idx)) {
            // This pin is already used, so it cannot be used for a wire.
            continue;
        }

        bool valid_pin_for_wire = false;
        bool invalid_pin_for_wire = false;

        for (const LutBel *lut_bel : unused_luts) {
            if (pin_idx < lut_bel->min_pin) {
                continue;
            }

            if (pin_idx > lut_bel->max_pin) {
                continue;
            }

            wire_bel_to_cell_pin_map.clear();
            wire_bel_to_cell_pin_map.resize(lut_bel->pins.size(), -1);
            wire_bel_to_cell_pin_map[lut_bel->pin_to_index.at(element.pins[pin_idx])] = 0;

            equation_result.clear();
            equation_result.resize(element.width, LL_DontCare);

            uint32_t used_pins_with_wire = used_pins | (1 << pin_idx);

            for (size_t cell_idx = 0; cell_idx < cells.size(); ++cell_idx) {
                const CellInfo *cell = cells[cell_idx];
                auto &lut_bel_for_cell = *lut_bels[cell_idx];
                if (!rotate_and_merge_lut_equation(&equation_result, lut_bel_for_cell, cell->lut_cell.equation,
                                                   bel_to_cell_pin_remaps[cell_idx], used_pins_with_wire)) {
                    invalid_pin_for_wire = true;
                    break;
                }
            }

            if (invalid_pin_for_wire) {
                break;
            }

            if (rotate_and_merge_lut_equation(&equation_result, *lut_bel, wire_equation, wire_bel_to_cell_pin_map,
                                              used_pins_with_wire)) {
                valid_pin_for_wire = true;
            }
        }

        bool good_for_wire = valid_pin_for_wire && !invalid_pin_for_wire;
        if (!good_for_wire) {
            vcc_mask |= (1 << pin_idx);
        }
    }

    return vcc_mask;
}

bool LutMapper::remap_luts(const Context *ctx)
{
    std::unordered_map<NetInfo *, LutPin> lut_pin_map;
    std::vector<const LutBel *> lut_bels;
    lut_bels.resize(cells.size());

    for (size_t cell_idx = 0; cell_idx < cells.size(); ++cell_idx) {
        const CellInfo *cell = cells[cell_idx];
#ifdef DEBUG_LUT_ROTATION
        log_info("Mapping %s %s eq = %s at %s\n", cell->type.c_str(ctx), cell->name.c_str(ctx),
                 cell->params.at(ctx->id("INIT")).c_str(), ctx->nameOfBel(cell->bel));
#endif

        auto &bel_data = bel_info(ctx->chip_info, cell->bel);
        IdString bel_name(bel_data.name);
        auto &lut_bel = element.lut_bels.at(bel_name);
        lut_bels[cell_idx] = &lut_bel;

        for (size_t pin_idx = 0; pin_idx < cell->lut_cell.pins.size(); ++pin_idx) {
            IdString lut_pin_name = cell->lut_cell.pins[pin_idx];
            const PortInfo &port_info = cell->ports.at(lut_pin_name);
            NPNR_ASSERT(port_info.net != nullptr);

            auto result = lut_pin_map.emplace(port_info.net, LutPin());
            LutPin &lut_pin = result.first->second;
            lut_pin.net = port_info.net;
            lut_pin.add_user(lut_bel, cell_idx, pin_idx);
        }
    }

    if (lut_pin_map.size() > element.pins.size()) {
        // Trival conflict, more nets entering element than pins are
        // available!
#ifdef DEBUG_LUT_ROTATION
        log_info("Trival failure %zu > %zu, %zu %zu\n", lut_pin_map.size(), element.pins.size(), element.width,
                 element.lut_bels.size());
#endif
        return false;
    }

    std::vector<LutPin> lut_pins;
    lut_pins.reserve(lut_pin_map.size());
    for (auto lut_pin_pair : lut_pin_map) {
        lut_pins.push_back(std::move(lut_pin_pair.second));
    }
    lut_pin_map.clear();
    std::sort(lut_pins.begin(), lut_pins.end());

    std::vector<std::vector<size_t>> cell_to_bel_pin_remaps;
    std::vector<std::vector<int32_t>> bel_to_cell_pin_remaps;
    cell_to_bel_pin_remaps.resize(cells.size());
    bel_to_cell_pin_remaps.resize(cells.size());
    for (size_t i = 0; i < cells.size(); ++i) {
        cell_to_bel_pin_remaps[i].resize(cells[i]->lut_cell.pins.size());
        bel_to_cell_pin_remaps[i].resize(lut_bels[i]->pins.size(), -1);
    }

    uint32_t used_pins = 0;
    size_t idx = 0;
    std::vector<IdString> net_pins(lut_pins.size());
    for (auto &lut_pin : lut_pins) {
        size_t net_idx = idx++;
        used_pins |= (1 << net_idx);

        for (auto cell_pin_idx : lut_pin.users) {
            size_t cell_idx = cell_pin_idx.cell_idx;
            size_t pin_idx = cell_pin_idx.cell_pin_idx;
            IdString bel_pin = lut_bels[cell_idx]->pins[net_idx];
#ifdef DEBUG_LUT_ROTATION
            log_info("%s %s %s => %s (%s)\n", cells[cell_idx]->type.c_str(ctx), cells[cell_idx]->name.c_str(ctx),
                     cells[cell_idx]->lut_cell.pins[pin_idx].c_str(ctx), bel_pin.c_str(ctx),
                     lut_pin.net->name.c_str(ctx));
#endif
            if (net_pins[net_idx] == IdString()) {
                net_pins[net_idx] = bel_pin;
            } else {
                NPNR_ASSERT(net_pins[net_idx] == bel_pin);
            }

            cell_to_bel_pin_remaps[cell_idx][pin_idx] = net_idx;
            bel_to_cell_pin_remaps[cell_idx][net_idx] = pin_idx;
        }
    }

    // Try to see if the equations are mergable!
    std::vector<LogicLevel> equation_result;
    equation_result.resize(element.width, LL_DontCare);
    for (size_t cell_idx = 0; cell_idx < cells.size(); ++cell_idx) {
        const CellInfo *cell = cells[cell_idx];
        auto &lut_bel = *lut_bels[cell_idx];
        if (!rotate_and_merge_lut_equation(&equation_result, lut_bel, cell->lut_cell.equation,
                                           bel_to_cell_pin_remaps[cell_idx], used_pins)) {
#ifdef DEBUG_LUT_ROTATION
            log_info("Failed to find a solution!\n");
            for (auto *cell : cells) {
                log_info("%s %s : %s\b\n", cell->type.c_str(ctx), cell->name.c_str(ctx),
                         cell->params.at(ctx->id("INIT")).c_str());
            }
#endif
            return false;
        }
    }

#ifdef DEBUG_LUT_ROTATION
    log_info("Found a solution!\n");
#endif

    // Sanity check final equation to make sure no assumptions are violated.
    if (kCheckOutputEquation) {
        for (size_t cell_idx = 0; cell_idx < cells.size(); ++cell_idx) {
            CellInfo *cell = cells[cell_idx];
            auto &lut_bel = *lut_bels[cell_idx];

            std::unordered_map<IdString, IdString> cell_to_bel_map;
            for (size_t pin_idx = 0; pin_idx < cell->lut_cell.pins.size(); ++pin_idx) {
                size_t bel_pin_idx = cell_to_bel_pin_remaps[cell_idx][pin_idx];
                NPNR_ASSERT(bel_pin_idx < lut_bel.pins.size());
                cell_to_bel_map[cell->lut_cell.pins[pin_idx]] = lut_bel.pins[bel_pin_idx];
            }

            check_equation(cell->lut_cell, cell_to_bel_map, lut_bel, equation_result, used_pins);
        }
    }

    // Push new cell -> BEL pin maps out to cells now that equations have been
    // verified!
    for (size_t cell_idx = 0; cell_idx < cells.size(); ++cell_idx) {
        CellInfo *cell = cells[cell_idx];
        auto &lut_bel = *lut_bels[cell_idx];

        for (size_t pin_idx = 0; pin_idx < cell->lut_cell.pins.size(); ++pin_idx) {
            auto &bel_pins = cell->cell_bel_pins[cell->lut_cell.pins[pin_idx]];
            bel_pins.clear();
            bel_pins.push_back(lut_bel.pins[cell_to_bel_pin_remaps[cell_idx][pin_idx]]);
        }
    }

    if (cells.size() == element.lut_bels.size()) {
        for (size_t cell_idx = 0; cell_idx < cells.size(); ++cell_idx) {
            CellInfo *cell = cells[cell_idx];
            auto &lut_bel = *lut_bels[cell_idx];
            cell->lut_cell.vcc_pins.clear();
            for (size_t bel_pin_idx = 0; bel_pin_idx < lut_bel.pins.size(); ++bel_pin_idx) {
                if ((used_pins & (1 << bel_pin_idx)) == 0) {
                    NPNR_ASSERT(bel_to_cell_pin_remaps[cell_idx][bel_pin_idx] == -1);
                    cell->lut_cell.vcc_pins.emplace(lut_bel.pins.at(bel_pin_idx));
                }
            }
        }
    } else {
        // Look to see if wires can be run from element inputs to unused
        // outputs. If not, block the BEL pin by tying to VCC.
        //
        // FIXME: The assumption is that unused pins are tied VCC.
        // This is not generally true.
        //
        // Use Arch::prefered_constant_net_type to determine what
        // constant net should be used for unused pins.
        uint32_t vcc_pins = check_wires(bel_to_cell_pin_remaps, lut_bels, used_pins);
#if defined(DEBUG_LUT_ROTATION)
        log_info("vcc_pins = 0x%x", vcc_pins);
        for (size_t cell_idx = 0; cell_idx < cells.size(); ++cell_idx) {
            CellInfo *cell = cells[cell_idx];
            log(", %s => %s", ctx->nameOfBel(cell->bel), cell->name.c_str(ctx));
        }
        log("\n");
#endif

        for (size_t cell_idx = 0; cell_idx < cells.size(); ++cell_idx) {
            CellInfo *cell = cells[cell_idx];
            auto &lut_bel = *lut_bels[cell_idx];
            cell->lut_cell.vcc_pins.clear();
            for (size_t bel_pin_idx = 0; bel_pin_idx < lut_bel.pins.size(); ++bel_pin_idx) {
                if ((vcc_pins & (1 << bel_pin_idx)) != 0) {
                    NPNR_ASSERT(bel_to_cell_pin_remaps[cell_idx][bel_pin_idx] == -1);
                    auto pin = lut_bel.pins.at(bel_pin_idx);
                    cell->lut_cell.vcc_pins.emplace(pin);
                }
            }
        }
    }

#ifdef DEBUG_LUT_ROTATION
    log_info("Final mapping:\n");
    for (size_t cell_idx = 0; cell_idx < cells.size(); ++cell_idx) {
        CellInfo *cell = cells[cell_idx];
        for (auto &cell_pin_pair : cell->cell_bel_pins) {
            log_info("%s %s %s =>", cell->type.c_str(ctx), cell->name.c_str(ctx), cell_pin_pair.first.c_str(ctx));
            for (auto bel_pin : cell_pin_pair.second) {
                log(" %s", bel_pin.c_str(ctx));
            }
            log("\n");
        }
    }
#endif

    return true;
}

void check_equation(const LutCell &lut_cell, const std::unordered_map<IdString, IdString> &cell_to_bel_map,
                    const LutBel &lut_bel, const std::vector<LogicLevel> &equation, uint32_t used_pins)
{
    std::vector<int8_t> pin_map;
    pin_map.resize(lut_bel.pins.size(), -1);

    NPNR_ASSERT(lut_cell.pins.size() < std::numeric_limits<decltype(pin_map)::value_type>::max());

    for (size_t cell_pin_idx = 0; cell_pin_idx < lut_cell.pins.size(); ++cell_pin_idx) {
        IdString cell_pin = lut_cell.pins[cell_pin_idx];
        IdString bel_pin = cell_to_bel_map.at(cell_pin);
        size_t bel_pin_idx = lut_bel.pin_to_index.at(bel_pin);

        pin_map[bel_pin_idx] = cell_pin_idx;
    }

    // Iterate over all BEL addresses in the LUT, and ensure that the original
    // LUT equation is respected.
    size_t bel_width = 1 << lut_bel.pins.size();
    NPNR_ASSERT(lut_bel.low_bit + bel_width == lut_bel.high_bit + 1);
    for (size_t bel_address = 0; bel_address < bel_width; ++bel_address) {
        LogicLevel level = equation[bel_address + lut_bel.low_bit];

        bool address_reachable = true;
        size_t cell_address = 0;
        for (size_t bel_pin_idx = 0; bel_pin_idx < lut_bel.pins.size(); ++bel_pin_idx) {
            // This address line is 0, so don't translate this bit to the cell
            // address.
            if ((bel_address & (1 << bel_pin_idx)) == 0) {
                // This pin is unused, so the line will be tied high, this
                // address is unreachable.
                if ((used_pins & (1 << bel_pin_idx)) == 0) {
                    address_reachable = false;
                    break;
                }
                continue;
            }

            auto cell_pin_idx = pin_map[bel_pin_idx];

            // Is this BEL pin used for this cell?
            if (cell_pin_idx < 0) {
                // This BEL pin is not used for the LUT cell, skip
                continue;
            }

            cell_address |= (1 << cell_pin_idx);
        }

        if (!address_reachable) {
            continue;
        }

        if (lut_cell.equation.get(cell_address)) {
            NPNR_ASSERT(level == LL_One);
        } else {
            NPNR_ASSERT(level == LL_Zero);
        }
    }
}

void LutElement::compute_pin_order()
{
    pins.clear();
    pin_to_index.clear();

    for (auto &lut_bel_pair : lut_bels) {
        auto &lut_bel = lut_bel_pair.second;

        for (size_t pin_idx = 0; pin_idx < lut_bel.pins.size(); ++pin_idx) {
            IdString pin = lut_bel.pins[pin_idx];
            auto result = pin_to_index.emplace(pin, pin_idx);
            if (!result.second) {
                // Not sure when this isn't true, but check it for now!
                NPNR_ASSERT(result.first->second == pin_idx);
            }
        }
    }

    pins.resize(pin_to_index.size());
    for (auto &pin_pair : pin_to_index) {
        pins.at(pin_pair.second) = pin_pair.first;
    }

    for (auto &lut_bel_pair : lut_bels) {
        auto &lut_bel = lut_bel_pair.second;
        lut_bel.min_pin = pin_to_index.at(lut_bel.pins.front());
        lut_bel.max_pin = pin_to_index.at(lut_bel.pins.back());
    }
}

NEXTPNR_NAMESPACE_END
